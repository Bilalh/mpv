/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Warning: this is outdated, crappy code. It is used only for --playlist.
 * New or cleaned up code should be added to demux_playlist.c instead.
 */

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

#include "talloc.h"
#include "asxparser.h"
#include "playlist.h"
#include "playlist_parser.h"
#include "stream/stream.h"
#include "demux/demux.h"
#include "common/global.h"
#include "common/msg.h"
#include "options/path.h"


#define BUF_STEP 1024

#define WHITES " \n\r\t"

typedef struct play_tree_parser {
  struct stream *stream;
  char *buffer,*iter,*line;
  int buffer_size , buffer_end;
  int keep;
  struct playlist *pl;
  struct mp_log *log;
} play_tree_parser_t;

static void
strstrip(char* str) {
  char* i;

  if (str==NULL)
    return;
  for(i = str ; i[0] != '\0' && strchr(WHITES,i[0]) != NULL; i++)
    /* NOTHING */;
  if(i[0] != '\0') {
    memmove(str,i,strlen(i) + 1);
    for(i = str + strlen(str) - 1 ; strchr(WHITES,i[0]) != NULL; i--)
      /* NOTHING */;
    i[1] = '\0';
  } else
    str[0] = '\0';
}

static char*
play_tree_parser_get_line(play_tree_parser_t* p) {
  char *end,*line_end;
  int r,resize = 0;

  if(p->buffer == NULL) {
    p->buffer = malloc(BUF_STEP);
    p->buffer_size = BUF_STEP;
    p->buffer[0] = 0;
    p->iter = p->buffer;
  }

  if(p->stream->eof && (p->buffer_end == 0 || p->iter[0] == '\0'))
    return NULL;

  assert(p->buffer_end < p->buffer_size);
  assert(!p->buffer[p->buffer_end]);
  while(1) {

    if(resize) {
      char *tmp;
      r = p->iter - p->buffer;
      end = p->buffer + p->buffer_end;
      if (p->buffer_size > INT_MAX - BUF_STEP)
        break;
      tmp = realloc(p->buffer, p->buffer_size + BUF_STEP);
      if (!tmp)
        break;
      p->buffer = tmp;
      p->iter = p->buffer + r;
      p->buffer_size += BUF_STEP;
      resize = 0;
    }

    if(p->buffer_size - p->buffer_end > 1 && ! p->stream->eof) {
      r = stream_read(p->stream,p->buffer + p->buffer_end,p->buffer_size - p->buffer_end - 1);
      if(r > 0) {
	p->buffer_end += r;
	assert(p->buffer_end < p->buffer_size);
	p->buffer[p->buffer_end] = '\0';
	while(strlen(p->buffer + p->buffer_end - r) != r)
	  p->buffer[p->buffer_end - r + strlen(p->buffer + p->buffer_end - r)] = '\n';
      }
      assert(!p->buffer[p->buffer_end]);
    }

    end = strchr(p->iter,'\n');
    if(!end) {
      if(p->stream->eof) {
	end = p->buffer + p->buffer_end;
	break;
      }
      resize = 1;
      continue;
    }
    break;
  }

  line_end = (end > p->iter && *(end-1) == '\r') ? end-1 : end;
  if(line_end - p->iter >= 0)
    p->line = realloc(p->line, line_end - p->iter + 1);
  else
    return NULL;
  if(line_end - p->iter > 0)
    strncpy(p->line,p->iter,line_end - p->iter);
  p->line[line_end - p->iter] = '\0';
  if(end[0] != '\0')
    end++;

  if(!p->keep) {
    if(end[0] != '\0') {
      p->buffer_end -= end-p->iter;
      memmove(p->buffer,end,p->buffer_end);
    } else
      p->buffer_end = 0;
    p->buffer[p->buffer_end] = '\0';
    p->iter = p->buffer;
  } else
    p->iter = end;

  return p->line;
}

static void
play_tree_parser_reset(play_tree_parser_t* p) {
  p->iter = p->buffer;
}

static void
play_tree_parser_stop_keeping(play_tree_parser_t* p) {
  p->keep = 0;
  if(p->iter && p->iter != p->buffer) {
    p->buffer_end -= p->iter -p->buffer;
    if(p->buffer_end)
      memmove(p->buffer,p->iter,p->buffer_end);
    p->buffer[p->buffer_end] = 0;
    p->iter = p->buffer;
  }
}


static bool parse_asx(play_tree_parser_t* p) {
  int comments = 0,get_line = 1;
  char* line = NULL;

  MP_VERBOSE(p, "Trying asx...\n");

  while(1) {
    if(get_line) {
      line = play_tree_parser_get_line(p);
      if(!line)
	return false;
      strstrip(line);
      if(line[0] == '\0')
	continue;
    }
    if(!comments) {
      if(line[0] != '<') {
	MP_DBG(p, "First char isn't '<' but '%c'\n",line[0]);
	MP_TRACE(p, "Buffer = [%s]\n",p->buffer);
	return false;
      } else if(strncmp(line,"<!--",4) == 0) { // Comments
	comments = 1;
	line += 4;
	if(line[0] != '\0' && strlen(line) > 0)
	  get_line = 0;
      } else if(strncasecmp(line,"<ASX",4) == 0) // We got an asx element
	break;
      else // We don't get an asx
	return false;
    } else { // Comments
      char* c;
      c = strchr(line,'-');
      if(c) {
	if (strncmp(c,"--!>",4) == 0) { // End of comments
	  comments = 0;
	  line = c+4;
	  if(line[0] != '\0') // There is some more data on this line : keep it
	    get_line = 0;

	} else {
	  line = c+1; // Jump the -
	  if(line[0] != '\0') // Some more data
	    get_line = 0;
	  else  // End of line
	    get_line = 1;
	}
      } else // No - on this line (or rest of line) : get next one
	get_line = 1;
    }
  }

  MP_VERBOSE(p, "Detected asx format\n");

  // We have an asx : load it in memory and parse

  while((line = play_tree_parser_get_line(p)) != NULL)
    /* NOTHING */;

 MP_TRACE(p, "Parsing asx file: [%s]\n",p->buffer);
 return asx_parse(p->buffer,p->pl,p->log);
}

static bool parse_smil(play_tree_parser_t* p) {
  int entrymode=0;
  char* line,source[512],*pos,*s_start,*s_end,*src_line;
  int is_rmsmil = 0;
  unsigned int npkt, ttlpkt;

  MP_VERBOSE(p, "Trying smil playlist...\n");

  // Check if smil
  while((line = play_tree_parser_get_line(p)) != NULL) {
    strstrip(line);
    if(line[0] == '\0') // Ignore empties
      continue;
    if (strncasecmp(line,"<?xml",5)==0) // smil in xml
      continue;
    if (strncasecmp(line,"<!DOCTYPE smil",13)==0) // smil in xml
      continue;
    if (strncasecmp(line,"<smil",5)==0 || strncasecmp(line,"<?wpl",5)==0 ||
      strncasecmp(line,"(smil-document",14)==0)
      break; // smil header found
    else
      return NULL; //line not smil exit
  }

  if (!line) return NULL;
  MP_VERBOSE(p, "Detected smil playlist format\n");
  play_tree_parser_stop_keeping(p);

  if (strncasecmp(line,"(smil-document",14)==0) {
    MP_VERBOSE(p, "Special smil-over-realrtsp playlist header\n");
    is_rmsmil = 1;
    if (sscanf(line, "(smil-document (ver 1.0)(npkt %u)(ttlpkt %u", &npkt, &ttlpkt) != 2) {
      MP_WARN(p, "smil-over-realrtsp: header parsing failure, assuming single packet.\n");
      npkt = ttlpkt = 1;
    }
    if (ttlpkt == 0 || npkt > ttlpkt) {
      MP_WARN(p, "smil-over-realrtsp: bad packet counters (npkk = %u, ttlpkt = %u), assuming single packet.\n",
        npkt, ttlpkt);
      npkt = ttlpkt = 1;
    }
  }

  //Get entries from smil
  src_line = line;
  line = NULL;
  do {
    strstrip(src_line);
    free(line);
    line = NULL;
    /* If we're parsing smil over realrtsp and this is not the last packet and
     * this is the last line in the packet (terminating with ") ) we must get
     * the next line, strip the header, and concatenate it to the current line.
     */
    if (is_rmsmil && npkt != ttlpkt && strstr(src_line,"\")")) {
      char *payload;

      line = strdup(src_line);
      if(!(src_line = play_tree_parser_get_line(p))) {
        MP_WARN(p, "smil-over-realrtsp: can't get line from packet %u/%u.\n", npkt, ttlpkt);
        break;
      }
      strstrip(src_line);
      // Skip header, packet starts after "
      if(!(payload = strchr(src_line,'\"'))) {
        MP_WARN(p, "smil-over-realrtsp: can't find start of packet, using complete line.\n");
        payload = src_line;
      } else
        payload++;
      // Skip ") at the end of the last line from the current packet
      line[strlen(line)-2] = 0;
      line = realloc(line, strlen(line)+strlen(payload)+1);
      strcat (line, payload);
      npkt++;
    } else
      line = strdup(src_line);
    /* Unescape \" to " for smil-over-rtsp */
    if (is_rmsmil && line[0] != '\0') {
      int i, j;

      for (i = 0; i < strlen(line); i++)
        if (line[i] == '\\' && line[i+1] == '"')
          for (j = i; line[j]; j++)
            line[j] = line[j+1];
    }
    pos = line;
   while (pos) {
    if (!entrymode) { // all entries filled so far
     while ((pos=strchr(pos, '<'))) {
      if (strncasecmp(pos,"<video",6)==0  || strncasecmp(pos,"<audio",6)==0 || strncasecmp(pos,"<media",6)==0) {
          entrymode=1;
          break; // Got a valid tag, exit '<' search loop
      }
      pos++;
     }
    }
    if (entrymode) { //Entry found but not yet filled
      pos = strstr(pos,"src=");   // Is source present on this line
      if (pos != NULL) {
        entrymode=0;
        if (pos[4] != '"' && pos[4] != '\'') {
          MP_VERBOSE(p, "Unknown delimiter %c in source line %s\n", pos[4], line);
          break;
        }
        s_start=pos+5;
        s_end=strchr(s_start,pos[4]);
        if (s_end == NULL) {
          MP_VERBOSE(p, "Error parsing this source line %s\n",line);
          break;
        }
        if (s_end-s_start> 511) {
          MP_VERBOSE(p, "Cannot store such a large source %s\n",line);
          break;
        }
        strncpy(source,s_start,s_end-s_start);
        source[(s_end-s_start)]='\0'; // Null terminate
        playlist_add_file(p->pl, source);
        pos = s_end;
      }
    }
   }
  } while((src_line = play_tree_parser_get_line(p)) != NULL);

  free(line);
  return true;
}

/**
 * \brief decode the base64 used in nsc files
 * \param in input string, 0-terminated
 * \param buf output buffer, must point to memory suitable for realloc,
 *            will be NULL on failure.
 * \return decoded length in bytes
 */
static int decode_nsc_base64(struct mp_log *log, char *in, char **buf) {
  int i, j, n;
  if (in[0] != '0' || in[1] != '2')
    goto err_out;
  in += 2; // skip prefix
  if (strlen(in) < 16) // error out if nothing to decode
    goto err_out;
  in += 12; // skip encoded string length
  n = strlen(in) / 4;
  *buf = realloc(*buf, n * 3);
  for (i = 0; i < n; i++) {
    uint8_t c[4];
    for (j = 0; j < 4; j++) {
      c[j] = in[4 * i + j];
      if (c[j] >= '0' && c[j] <= '9') c[j] += 0 - '0';
      else if (c[j] >= 'A' && c[j] <= 'Z') c[j] += 10 - 'A';
      else if (c[j] >= 'a' && c[j] <= 'z') c[j] += 36 - 'a';
      else if (c[j] == '{') c[j] = 62;
      else if (c[j] == '}') c[j] = 63;
      else {
        mp_err(log, "Invalid character %c (0x%02"PRIx8")\n", c[j], c[j]);
        goto err_out;
      }
    }
    (*buf)[3 * i] = (c[0] << 2) | (c[1] >> 4);
    (*buf)[3 * i + 1] = (c[1] << 4) | (c[2] >> 2);
    (*buf)[3 * i + 2] = (c[2] << 6) | c[3];
  }
  return 3 * n;
err_out:
  free(*buf);
  *buf = NULL;
  return 0;
}

/**
 * \brief "converts" utf16 to ascii by just discarding every second byte
 * \param buf buffer to convert
 * \param len lenght of buffer, must be > 0
 */
static void utf16_to_ascii(char *buf, int len) {
  int i;
  if (len <= 0) return;
  for (i = 0; i < len / 2; i++)
    buf[i] = buf[i * 2];
  buf[i] = 0; // just in case
}

static bool parse_nsc(play_tree_parser_t* p) {
  char *line, *addr = NULL, *url, *unicast_url = NULL;
  int port = 0;

  MP_VERBOSE(p, "Trying nsc playlist...\n");
  while((line = play_tree_parser_get_line(p)) != NULL) {
    strstrip(line);
    if(!line[0]) // Ignore empties
      continue;
    if (strncasecmp(line,"[Address]", 9) == 0)
      break; // nsc header found
    else
      return false;
  }
  MP_VERBOSE(p, "Detected nsc playlist format\n");
  play_tree_parser_stop_keeping(p);
  while ((line = play_tree_parser_get_line(p)) != NULL) {
    strstrip(line);
    if (!line[0])
      continue;
    if (strncasecmp(line, "Unicast URL=", 12) == 0) {
      int len = decode_nsc_base64(p->log, &line[12], &unicast_url);
      if (len <= 0)
        MP_WARN(p, "[nsc] Unsupported Unicast URL encoding\n");
      else
        utf16_to_ascii(unicast_url, len);
    } else if (strncasecmp(line, "IP Address=", 11) == 0) {
      int len = decode_nsc_base64(p->log, &line[11], &addr);
      if (len <= 0)
        MP_WARN(p, "[nsc] Unsupported IP Address encoding\n");
      else
        utf16_to_ascii(addr, len);
    } else if (strncasecmp(line, "IP Port=", 8) == 0) {
      port = strtol(&line[8], NULL, 0);
    }
  }

  bool success = false;

  if (unicast_url)
    url = strdup(unicast_url);
  else if (addr && port) {
    url = malloc(strlen(addr) + 7 + 20 + 1);
    sprintf(url, "http://%s:%i", addr, port);
  } else
   goto err_out;

  playlist_add_file(p->pl, url);
  free(url);
  success = true;
err_out:
  free(addr);
  free(unicast_url);
  return success;
}

static struct playlist *do_parse(struct stream* stream, bool forced,
                                 struct mp_log *log, struct mpv_global *global);

struct playlist *playlist_parse_file(const char *file, struct mpv_global *global)
{
  struct mp_log *log = mp_log_new(NULL, global->log, "!playlist_parser");
  struct playlist *ret = NULL;
  stream_t *stream = stream_open(file, global);
  if(!stream) {
      mp_err(log, "Error while opening playlist file %s: %s\n",
             file, strerror(errno));
    goto done;
  }

  mp_verbose(log, "Parsing playlist file %s...\n", file);

  ret = do_parse(stream, true, log, global);
  free_stream(stream);

  if (ret)
    playlist_add_base_path(ret, mp_dirname(file));

done:
  talloc_free(log);
  return ret;
}

typedef bool (*parser_fn)(play_tree_parser_t *);
static const parser_fn pl_parsers[] = {
    parse_asx,
    parse_smil,
    parse_nsc,
};


static struct playlist *do_parse(struct stream* stream, bool forced,
                                 struct mp_log *log, struct mpv_global *global)
{
  play_tree_parser_t p = {
      .stream = stream,
      .pl = talloc_zero(NULL, struct playlist),
      .keep = 1,
      .log = log,
  };

  bool success = false;
  struct demuxer *pl_demux = demux_open(stream, "playlist", NULL, global);
  if (pl_demux && pl_demux->playlist) {
    playlist_transfer_entries(p.pl, pl_demux->playlist);
    success = true;
  }
  free_demuxer(pl_demux);
  if (!success && play_tree_parser_get_line(&p) != NULL) {
    for (int n = 0; n < sizeof(pl_parsers) / sizeof(pl_parsers[0]); n++) {
      play_tree_parser_reset(&p);
      if (pl_parsers[n](&p)) {
        success = true;
        break;
      }
    }
  }

  if(success)
    mp_verbose(log, "Playlist successfully parsed\n");
  else {
    mp_msg(log,((forced==1)?MSGL_ERR:MSGL_V),"Error while parsing playlist\n");
    talloc_free(p.pl);
    p.pl = NULL;
  }

  if (p.pl && !p.pl->first)
    mp_msg(log, ((forced==1)?MSGL_WARN:MSGL_V),"Warning: empty playlist\n");

  return p.pl;
}
