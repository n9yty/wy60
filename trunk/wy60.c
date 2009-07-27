/*
 * Copyright (C) 2001, 2002 Markus Gutschke <markus+wy60@wy60.gutschke.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "wy60.h"

#undef DEBUG_LOG_SESSION
#undef DEBUG_LOG_NATIVE
#undef DEBUG_LOG_HOST
#undef DEBUG_SINGLE_STEP
#undef DEBUG_DECODE


#define WY60_VERSION PACKAGE_NAME" v"PACKAGE_VERSION" (" __DATE__ ")"


enum { E_NORMAL, E_ESC, E_SKIP_ONE, E_SKIP_LINE, E_SKIP_DEL, E_FILL_SCREEN,
       E_GOTO_SEGMENT, E_GOTO_ROW_CODE, E_GOTO_COLUMN_CODE, E_GOTO_ROW,
       E_GOTO_COLUMN, E_SET_FIELD_ATTRIBUTE, E_SET_ATTRIBUTE,
       E_GRAPHICS_CHARACTER, E_SET_FEATURES, E_FUNCTION_KEY,
       E_SET_SEGMENT_POSITION, E_SELECT_PAGE, E_CSI_E };
enum { T_NORMAL = 0, T_BLANK = 1, T_BLINK = 2, T_REVERSE = 4,
       T_UNDERSCORE = 8, T_DIM = 64, T_BOTH = 68, T_ALL = 79,
       T_PROTECTED = 256, T_GRAPHICS = 512 };
enum { J_AUTO = 0, J_ON, J_OFF };

typedef struct KeyDefs {
  struct KeyDefs *left, *right, *down;
  char           ch;
  const char     *name;
  const char     *nativeKeys;
  const char     *wy60Keys;
} KeyDefs;


typedef struct ScreenBuffer {
  unsigned short **attributes;
  char           **lineBuffer;
  int            cursorX;
  int            cursorY;
  int            maximumWidth;
  int            maximumHeight;
} ScreenBuffer;


static int            euid, egid, uid, gid, oldStylePty, streamsIO, jobControl;
static char           ptyName[40];
static struct termios defaultTermios;
static sigjmp_buf     mainJumpBuffer, auxiliaryJumpBuffer;
static int            useAuxiliarySignalHandler;
static int            needsReset, needsClearingBuffers;
static int            screenWidth, screenHeight, originalWidth, originalHeight;
static int            nominalWidth, nominalHeight;
static int            mode, protected, writeProtection, currentAttributes;
static int            normalAttributes, protectedAttributes = T_REVERSE;
static int            protectedPersonality = T_REVERSE;
static int            insertMode, graphicsMode, cursorIsHidden, currentPage;
static int            changedDimensions, targetColumn, targetRow;
static ScreenBuffer   *screenBuffer[3], *currentBuffer;
static char           extraData[1024];
static int            extraDataLength;
static int            vtStyleCursorReporting;
static int            wyStyleCursorReporting;
static KeyDefs        *keyDefinitions, *currentKeySequence;
static char           *commandName;
static int            loginShell, isLoginWrapper;
static char           outputBuffer[16384];
static int            outputBufferLength;
static char           inputBuffer[128];
static int            inputBufferLength;


static char *cfgTerm            = "wyse60";
static char *cfgShell           = "/bin/sh";
static char *cfgResize          = "";
static char *cfgWriteProtect    = "";
static char *cfgA1              = "";
static char *cfgA3              = "";
static char *cfgB2              = "";
static char *cfgC1              = "";
static char *cfgC3              = "";
static char *cfgBackspace       = "\x08";
static char *cfgBacktab         = "\x1BI";
static char *cfgBegin           = "";
static char *cfgCancel          = "";
static char *cfgClear           = "";
static char *cfgClearAllTabs    = "";
static char *cfgClearTab        = "";
static char *cfgClose           = "";
static char *cfgCommand         = "";
static char *cfgCopy            = "";
static char *cfgCreate          = "";
static char *cfgDelete          = "\x1BR";
static char *cfgDeleteLine      = "\x1BW";
static char *cfgDown            = "\x0A";
static char *cfgEnd             = "\x1BT";
static char *cfgEndOfLine       = "\x1BY";
static char *cfgEndOfScreen     = "\x1BT";
static char *cfgEnter           = "\x1B""7";
static char *cfgExit            = "";
static char *cfgExitInsertMode  = "";
static char *cfgFind            = "";
static char *cfgHelp            = "";
static char *cfgHome            = "\x1E";
static char *cfgInsert          = "\x1B""E";
static char *cfgInsertLine      = "\x1BQ";
static char *cfgLeft            = "\x08";
static char *cfgLowerLeft       = "";
static char *cfgMark            = "";
static char *cfgMessage         = "";
static char *cfgMove            = "";
static char *cfgNext            = "\x1BK";
static char *cfgOpen            = "";
static char *cfgOptions         = "";
static char *cfgPageDown        = "\x1BK";
static char *cfgPageUp          = "\x1BJ";
static char *cfgPrevious        = "\x1BJ";
static char *cfgPrint           = "\x1BP";
static char *cfgRedo            = "";
static char *cfgReference       = "";
static char *cfgRefresh         = "";
static char *cfgReplace         = "\x1Br";
static char *cfgRestart         = "";
static char *cfgResume          = "";
static char *cfgRight           = "\x0C";
static char *cfgSave            = "";
static char *cfgScrollDown      = "";
static char *cfgScrollUp        = "";
static char *cfgSelect          = "";
static char *cfgSetTab          = "";
static char *cfgSuspend         = "\x1A";
static char *cfgUndo            = "";
static char *cfgUp              = "\x0B";
static char *cfgShiftBegin      = "";
static char *cfgShiftCancel     = "";
static char *cfgShiftCommand    = "";
static char *cfgShiftCopy       = "";
static char *cfgShiftCreate     = "";
static char *cfgShiftDelete     = "\x1BR";
static char *cfgShiftDeleteLine = "\x1BW";
static char *cfgShiftEnd        = "\x1BT";
static char *cfgShiftEndOfLine  = "\x1BT";
static char *cfgShiftExit       = "";
static char *cfgShiftFind       = "";
static char *cfgShiftHelp       = "";
static char *cfgShiftHome       = "\x1B{";
static char *cfgShiftInsert     = "\x1BQ";
static char *cfgShiftLeft       = "\x08";
static char *cfgShiftMessage    = "";
static char *cfgShiftMove       = "";
static char *cfgShiftNext       = "\x1BK";
static char *cfgShiftOptions    = "";
static char *cfgShiftPrevious   = "\x1BJ";
static char *cfgShiftPrint      = "\x1BP";
static char *cfgShiftRedo       = "";
static char *cfgShiftReplace    = "\x1Br";
static char *cfgShiftResume     = "";
static char *cfgShiftRight      = "\x0C";
static char *cfgShiftSave       = "";
static char *cfgShiftSuspend    = "";
static char *cfgShiftUndo       = "";
static char *cfgF0              = "";
static char *cfgF1              = "\x01@\r";
static char *cfgF2              = "\x01""A\r";
static char *cfgF3              = "\x01""B\r";
static char *cfgF4              = "\x01""C\r";
static char *cfgF5              = "\x01""D\r";
static char *cfgF6              = "\x01""E\r";
static char *cfgF7              = "\x01""F\r";
static char *cfgF8              = "\x01G\r";
static char *cfgF9              = "\x01H\r";
static char *cfgF10             = "\x01I\r";
static char *cfgF11             = "\x01J\r";
static char *cfgF12             = "\x01K\r";
static char *cfgF13             = "\x01`\r";
static char *cfgF14             = "\x01""a\r";
static char *cfgF15             = "\x01""b\r";
static char *cfgF16             = "\x01""c\r";
static char *cfgF17             = "\x01""d\r";
static char *cfgF18             = "\x01""e\r";
static char *cfgF19             = "\x01""f\r";
static char *cfgF20             = "\x01g\r";
static char *cfgF21             = "\x01h\r";
static char *cfgF22             = "\x01i\r";
static char *cfgF23             = "\x01j\r";
static char *cfgF24             = "\x01k\r";
static char *cfgF25             = "\x01L\r";
static char *cfgF26             = "\x01M\r";
static char *cfgF27             = "\x01N\r";
static char *cfgF28             = "\x01O\r";
static char *cfgF29             = "\x01l\r";
static char *cfgF30             = "\x01m\r";
static char *cfgF31             = "\x01n\r";
static char *cfgF32             = "\x01o\r";
static char *cfgF33             = "";
static char *cfgF34             = "";
static char *cfgF35             = "";
static char *cfgF36             = "";
static char *cfgF37             = "";
static char *cfgF38             = "";
static char *cfgF39             = "";
static char *cfgF40             = "";
static char *cfgF41             = "";
static char *cfgF42             = "";
static char *cfgF43             = "";
static char *cfgF44             = "";
static char *cfgF45             = "";
static char *cfgF46             = "";
static char *cfgF47             = "";
static char *cfgF48             = "";
static char *cfgF49             = "";
static char *cfgF50             = "";
static char *cfgF51             = "";
static char *cfgF52             = "";
static char *cfgF53             = "";
static char *cfgF54             = "";
static char *cfgF55             = "";
static char *cfgF56             = "";
static char *cfgF57             = "";
static char *cfgF58             = "";
static char *cfgF59             = "";
static char *cfgF60             = "";
static char *cfgF61             = "";
static char *cfgF62             = "";
static char *cfgF63             = "";


#ifdef __GNUC__
#define expandParm(buffer, parm, args...) ({               \
  char *tmp = parm ? tparm(parm, ##args) : NULL;           \
  if (tmp && strlen(tmp) < sizeof(buffer))                 \
    tmp = strcpy(buffer, tmp);                             \
  else                                                     \
    tmp = NULL;                                            \
  tmp; })
#define expandParm2 expandParm
#define expandParm9 expandParm
#else
#define expandParm(buffer, parm, arg)                      \
  ((parm) ? _expandParmCheck((buffer),                     \
                             tparm((parm), (arg)),         \
                             sizeof(buffer)) : NULL)
#define expandParm2(buffer, parm, arg1, arg2)              \
  ((parm) ? _expandParmCheck((buffer),                     \
                             tparm((parm), (arg1), (arg2)),\
                             sizeof(buffer)) : NULL)
#define expandParm9(buffer, parm, arg1, arg2, arg3, arg4,  \
                    arg5, arg6, arg7, arg8, arg9)          \
  ((parm) ? _expandParmCheck((buffer),                     \
                             tparm((parm), (arg1), (arg2), \
                                   (arg3), (arg4), (arg5), \
                                   (arg6), (arg7), (arg8), \
                                   (arg9)),                \
                             sizeof(buffer)) : NULL)
static char *_expandParmCheck(char *buffer, const char *data, int size) {
  if (data && strlen(data) < size)
    return(strcpy(buffer, data));
  return(NULL);
}
#endif


#ifdef DEBUG_LOG_SESSION
static void logCharacters(int mode, const char *buffer, int size) {
  static int     logFd = -2;
  static long    lastTimeStamp;
  struct timeval timeValue;

  if (logFd == -2) {
    char *logger;

    if ((logger        = getenv("WY60LOGFILE")) != NULL) {
      logFd            = creat(logger, 0644);
      gettimeofday(&timeValue, 0);
      lastTimeStamp    = timeValue.tv_sec*10 + timeValue.tv_usec/100000;
    } else
      logFd            = -1;
  }
  if (logFd >= 0) {
    static void flushConsole(void);
    int  header[4];

    gettimeofday(&timeValue, 0);
    header[0]          = htonl(sizeof(header));
    header[1]          = htonl(size + sizeof(header));
    header[2]          = htonl(mode);
    header[3]          = htonl(timeValue.tv_sec*10 + timeValue.tv_usec/100000 -
                               lastTimeStamp);
    lastTimeStamp      = timeValue.tv_sec*10 + timeValue.tv_usec/100000;
    flushConsole();
    write(logFd, header, sizeof(header));
    write(logFd, buffer, size);
  }
  return;
}
#else
#define logCharacters(mode,buffer,size) do {} while (0)
#endif


#ifdef DEBUG_LOG_HOST
static void logHostCharacter(int mode, char ch) {
  static int logFd = -2;

  if (logFd == -2) {
    char *logger;

    if ((logger    = getenv("WY60HOST")) != NULL) {
      logFd        = creat(logger, 0644);
    } else
      logFd        = -1;
  }

  if (logFd >= 0) {
    static void flushConsole(void);
    char buffer[80];

    if (isatty(logFd))
      strcpy(buffer, mode ? "\x1B[35m" : "\x1B[32m");
    else
      *buffer      = '\000';
    if ((unsigned char)ch < (unsigned char)' ') {
      sprintf(strchr(buffer, '\000'), "^%c", ch | '@');
    } else if (ch & 0x80) {
      sprintf(strchr(buffer, '\000'), "\\x%02X", ch);
    } else {
      sprintf(strchr(buffer, '\000'), "%c", ch);
    }
    if (isatty(logFd))
      strcat(buffer, "\x1B[39m");

    flushConsole();
    write(logFd, buffer, strlen(buffer));
  }
}


static void logHostString(const char *buffer) {
  for (; *buffer; buffer++)
    logHostCharacter(0, *buffer);
  return;
}

static void logHostKey(char ch) {
  logHostCharacter(1, ch);
  return;
}
#else
#define logHostCharacter(m,ch) do {} while (0)
#define logHostString(buffer)  do {} while (0)
#define logHostKey(ch)         do {} while (0)
#endif


#ifdef DEBUG_DECODE
static char _decodeBuffer[1024];
static int  _decodeFd = -2;


static void logDecode(const char *format, ...) {

  if (_decodeFd == -2) {
    char *logger;
    if ((logger     = getenv("WY60DECODE")) != NULL) {
      _decodeFd     = creat(logger, 0644);
    } else
      _decodeFd     = -1;
  }

  if (_decodeFd >= 0) {
    va_list argList;
    char    *ptr;
    int     len;

    va_start(argList, format);
    if (!*_decodeBuffer && isatty(_decodeFd))
      strcpy(_decodeBuffer, "\x1B[34m");
    ptr             = strrchr(_decodeBuffer, '\000');
    len             = &_decodeBuffer[sizeof(_decodeBuffer) - 7] - ptr;
    vsnprintf(ptr, len, format, argList);
    va_end(argList);
  }
  return;
}


static void logDecodeFlush(void) {
  if (_decodeFd >= 0) {
    static void flushConsole(void);

    if (isatty(_decodeFd))
      strcat(_decodeBuffer, "\x1B[39m");
    strcat(_decodeBuffer, "\r\n");
    flushConsole();
    write(_decodeFd, _decodeBuffer, strlen(_decodeBuffer));
    *_decodeBuffer = '\000';
  }
  return;
}
#else
#ifdef __GNUC__
#define logDecode(format,args...) do {} while (0)
#define logDecodeFlush()          do {} while (0)
#else
static void logDecode(const char *format, ...) { return; }
static void logDecodeFlush(void) { return; }
#endif
#endif


static void dropPrivileges(void) {
  static int initialized;

  if (!initialized) {
    euid        = geteuid();
    egid        = getegid();
    uid         = getuid();
    gid         = getgid();
    initialized = 1;
  }
  seteuid(uid);
  setegid(gid);
  return;
}


static void assertPrivileges(void) {
  seteuid(euid);
  setegid(egid);
  return;
}


static void _clearScreenBuffer(ScreenBuffer *screenBuffer,
                               int x1, int y1, int x2, int y2,
                               unsigned short attributes, char fillChar) {
  if (x1 <= x2 && y1 <= y2) {
    int x, y;

    for (y = y1; y <= y2; y++) {
      unsigned short *attributesPtr = &screenBuffer->attributes[y][x1];
      char *linePtr                 = &screenBuffer->lineBuffer[y][x1];
      
      for (x = x1; x <= x2; x++) {
        *attributesPtr++            = attributes;
      }
      memset(linePtr, fillChar, x2 - x1 + 1);
    }
  }
  return;
}


static void clearScreenBuffer(ScreenBuffer *screenBuffer,
                              int x1, int y1, int x2, int y2,
                              unsigned short attributes, char fillChar) {
  if (x1 < 0)
    x1                              = 0;
  if (x2 >= screenWidth)
    x2                              = screenWidth - 1;
  if (y1 < 0)
    y1                              = 0;
  if (y2 >= screenHeight)
    y2                              = screenHeight - 1;
  _clearScreenBuffer(screenBuffer, x1, y1, x2, y2, attributes, fillChar);
  return;
}


static void clearExcessBuffers(void) {
  if (needsClearingBuffers) {
    int i;
    for (i = 0; i < sizeof(screenBuffer)/sizeof(ScreenBuffer *); i++) {
      _clearScreenBuffer(screenBuffer[i],
                         0, screenHeight,
                         screenBuffer[i]->maximumWidth - 1,
                         screenBuffer[i]->maximumHeight - 1,
                         T_NORMAL, ' ');
      _clearScreenBuffer(screenBuffer[i],
                         screenWidth, 0,
                         screenBuffer[i]->maximumWidth - 1,
                         screenHeight - 1,
                         T_NORMAL, ' ');
    }
    needsClearingBuffers        = 0;
  }
  return;
}


static void _moveScreenBuffer(ScreenBuffer *screenBuffer,
                              int x1, int y1, int x2, int y2,
                              int dx, int dy) {
  /* This code cannot properly move both horizontally and vertically at      */
  /* the same time; but we never need that anyway.                           */
  int y, w, h, left, right, up, down;

  left                              = dx < 0 ? -dx : 0;
  right                             = dx > 0 ?  dx : 0;
  up                                = dy < 0 ? -dy : 0;
  down                              = dy > 0 ?  dy : 0;
  if (x1 < left)
    x1                              = left;
  if (x2 >= screenWidth - right)
    x2                              = screenWidth - right - 1;
  if (y1 < up)
    y1                              = up;
  if (y2 >= screenHeight - down)
    y2                              = screenHeight - down - 1;
  w                                 = x2 - x1 + 1;
  h                                 = y2 - y1 + 1;
  if (w > 0 && h > 0) {
    if (dy < 0) {
      /* Moving up                                                           */
      for (y = y1; y <= y2; y++) {
        memmove(&screenBuffer->attributes[y + dy][x1 + dx],
                &screenBuffer->attributes[y     ][x1     ],
                w * sizeof(unsigned short));
        memmove(&screenBuffer->lineBuffer[y + dy][x1 + dx],
                &screenBuffer->lineBuffer[y     ][x1     ],
                w * sizeof(char));
      }
    } else {
      /* Moving down                                                         */
      for (y = y2 + 1; --y >= y1; ) {
        memmove(&screenBuffer->attributes[y + dy][x1 + dx],
                &screenBuffer->attributes[y     ][x1     ],
                w * sizeof(unsigned short));
        memmove(&screenBuffer->lineBuffer[y + dy][x1 + dx],
                &screenBuffer->lineBuffer[y     ][x1     ],
                w * sizeof(char));
      }
    }
  }
  if (dx > 0)
    clearScreenBuffer(screenBuffer, x1, y1, x1 + dx - 1, y2, T_NORMAL, ' ');
  else if (dx < 0)
    clearScreenBuffer(screenBuffer, x2 + dx + 1, y1, x2, y2, T_NORMAL, ' ');
  if (dy > 0)
    clearScreenBuffer(screenBuffer, x1, y1, x2, y1 + dy - 1, T_NORMAL, ' ');
  else if (dy < 0)
    clearScreenBuffer(screenBuffer, x1, y2 + dy + 1, x2, y2, T_NORMAL, ' ');
  return;
}


static void moveScreenBuffer(ScreenBuffer *screenBuffer,
                             int x1, int y1, int x2, int y2,
                             int dx, int dy) {
  clearExcessBuffers();
  _moveScreenBuffer(screenBuffer, x1, y1, x2, y2, dx, dy);
  return;
}


static ScreenBuffer *allocateScreenBuffer(int width, int height) {
  unsigned short *attributesPtr;
  char           *linePtr;
  int            i;

  size_t attributesMemorySize   = width * height * sizeof(unsigned short);
  size_t screenMemorySize       = width * height * sizeof(char);
  size_t lineMemorySize         = sizeof(char *) * height;
  ScreenBuffer *screenBuffer    = malloc(sizeof(ScreenBuffer) +
                                       screenMemorySize + attributesMemorySize+
                                       2*lineMemorySize);
  screenBuffer->attributes      = (unsigned short **)&screenBuffer[1];
  screenBuffer->lineBuffer      = (char **)&screenBuffer->attributes[height];
  screenBuffer->cursorX         =
  screenBuffer->cursorY         = 0;
  screenBuffer->maximumWidth    = width;
  screenBuffer->maximumHeight   = height;
  for (attributesPtr = (unsigned short *)&screenBuffer->lineBuffer[height],i=0;
       i < height;
       attributesPtr += width, i++) {
    screenBuffer->attributes[i] = attributesPtr;
  }
  for (linePtr = (char *)attributesPtr, i = 0;
       i < height;
       linePtr += width, i++) {
    screenBuffer->lineBuffer[i] = linePtr;
  }
  _clearScreenBuffer(screenBuffer, 0, 0, width-1, height-1, T_NORMAL, ' ');
  return(screenBuffer);
}


static ScreenBuffer *adjustScreenBuffer(ScreenBuffer *screenBuffer,
                                        int width, int height) {
  needsClearingBuffers          = 1;
  if (width < 1)
    width                       = 1;
  if (height < 1)
    height                      = 1;
  if (screenBuffer == NULL)
    screenBuffer                = allocateScreenBuffer(width, height);
  else if (width  > screenBuffer->maximumWidth ||
	   height > screenBuffer->maximumHeight) {
    /* Screen buffers only ever grow in size, they never shrink back to      */
    /* smaller dimensions even if the user shrunk the screen size. This      */
    /* doesn't waste much space, and allows for reducing the number of times */
    /* that we need to reallocate memory; also, it allows us to redraw old   */
    /* screen content if the screen size grows back.                         */
    int          i;
    int          tmpWidth       = width > screenBuffer->maximumWidth
                                  ? width : screenBuffer->maximumWidth;
    int          tmpHeight      = height > screenBuffer->maximumHeight
                                  ? height : screenBuffer->maximumHeight;
    ScreenBuffer *newBuffer     = allocateScreenBuffer(tmpWidth, tmpHeight);
    newBuffer->cursorX          = screenBuffer->cursorX;
    newBuffer->cursorY          = screenBuffer->cursorY;
    tmpWidth                    = screenBuffer->maximumWidth;
    tmpHeight                   = screenBuffer->maximumHeight;
    for (i = 0; i < tmpHeight; i++) {
      memcpy(newBuffer->attributes[i], screenBuffer->attributes[i],
             tmpWidth * sizeof(unsigned short));
      memcpy(newBuffer->lineBuffer[i], screenBuffer->lineBuffer[i],
             tmpWidth * sizeof(char));
    }
    free(screenBuffer);
    screenBuffer                = newBuffer;
  }
  if (screenBuffer->cursorX >= width)
    screenBuffer->cursorX       = width-1;
  if (screenBuffer->cursorY >= height)
    screenBuffer->cursorY       = height-1;
  return(screenBuffer);
}


static void displayCurrentScreenBuffer(void) {
  static void flushConsole(void);
  static void gotoXYforce(int x, int y);
  static void putCapability(const char *capability);
  static int  putConsole(int ch);
  static void putGraphics(char ch);
  static void showCursor(int flag);
  static void updateAttributes(void);

  int x, y, lastAttributes      = -1;
  int oldX                      = currentBuffer->cursorX;
  int oldY                      = currentBuffer->cursorY;
  int oldNormalAttributes       = normalAttributes;
  int oldProtectedAttributes    = protectedAttributes;
  int oldProtected              = protected;
  int oldCursorVisibility       = cursorIsHidden;

  showCursor(0);
  for (y = screenHeight; y-- > 0; ) {
    unsigned short*attributesPtr= currentBuffer->attributes[y];
    char          *linePtr      = currentBuffer->lineBuffer[y];
    if (y == screenHeight-1 && y > 0) {
      /* Outputting the very last character on the screen is difficult. We   */
      /* work around this problem by printing the last line one line too     */
      /* high and then scrolling it into place.                              */
      gotoXYforce(0, y-1);
      currentBuffer->cursorY    = y;
    } else
      gotoXYforce(0, y);
    for (x = 0; x < screenWidth; x++) {
      unsigned short attributes = *attributesPtr++;
      char character            = *linePtr++;
      if (attributes != lastAttributes) {
        protected               = !!(attributes & T_PROTECTED);
        normalAttributes        =
        protectedAttributes     = attributes & T_ALL;
        updateAttributes();
        lastAttributes          = attributes;
      }
      if (attributes & T_GRAPHICS)
        putGraphics(character);
      else
        putConsole(character);
      currentBuffer->cursorX++;
    }
    if (y == screenHeight-1 && y > 0) {
      gotoXYforce(0, y-1);
      if (insert_line && strcmp(insert_line, "@"))
        putCapability(insert_line);
      else {
        char buffer[1024];
        
        putCapability(expandParm(buffer, parm_insert_line, 1));
      }
    }
  }
  gotoXYforce(oldX, oldY);
  normalAttributes              = oldNormalAttributes;
  protectedAttributes           = oldProtectedAttributes;
  protected                     = oldProtected;
  updateAttributes();
  showCursor(!oldCursorVisibility);
  flushConsole();
  return;
}


static void flushConsole(void) {
  if (outputBufferLength) {
    write(1, outputBuffer, outputBufferLength);
    outputBufferLength = 0;
  }
  return;
}


static void writeConsole(const char *buffer, int len) {
  while (len > 0) {
    int i               = sizeof(outputBuffer) - outputBufferLength;
    if (len < i)
      i                 = len;
    memmove(outputBuffer + outputBufferLength, buffer, i);
    outputBufferLength += i;
    len                -= i;
    if (outputBufferLength == sizeof(outputBuffer))
      flushConsole();
  }
  return;
}


static void flushUserInput(int pty) {
  if (inputBufferLength) {
    write(pty, inputBuffer, inputBufferLength);
    inputBufferLength = 0;
  }
  return;
}


static void sendUserInput(int pty, const char *buffer, int len) {
  logCharacters(0, buffer, len);
  while (len > 0) {
    int i               = sizeof(inputBuffer) - inputBufferLength;
    if (len < i)
      i                 = len;
    memmove(inputBuffer + inputBufferLength, buffer, i);
    inputBufferLength  += i;
    len                -= i;
    if (inputBufferLength == sizeof(inputBuffer))
      flushUserInput(pty);
  }
  return;
}


static int _putConsole(int ch) {
  char c = (char)ch;

  writeConsole(&c, 1);
  return(ch);
}


static int putConsole(int ch) {
  _putConsole(ch);
  if (currentBuffer->cursorX >= 0 && currentBuffer->cursorY >= 0 &&
      currentBuffer->cursorX < screenWidth &&
      currentBuffer->cursorY < screenHeight) {
    unsigned short attributes = (unsigned short)currentAttributes;

    currentBuffer->lineBuffer[currentBuffer->cursorY]
                             [currentBuffer->cursorX] = ch & 0xFF;
    if (protected)
      attributes             |= T_PROTECTED;
    currentBuffer->attributes[currentBuffer->cursorY]
                             [currentBuffer->cursorX] = attributes;
  }
  return(ch);
}


static void putCapability(const char *capability) {
  static void failure(int exitCode, const char *message, ...);

  if (!capability || !strcmp(capability, "@"))
    failure(127, "Terminal has insufficient capabilities");
  logHostString(capability);
  ((int (*)(const char *, int, int (*)(int)))tputs)(capability, 1,_putConsole);
  return;
}


static void gotoXY(int x, int y) {
  static const int  UNDEF      = 65536;
  static char       absolute[1024], horizontal[1024], vertical[1024];
  int               absoluteLength, horizontalLength, verticalLength;
  int               i;
  int               jumpedHome = 0;

  if (x >= screenWidth)
    x                          = screenWidth - 1;
  if (x < 0)
    x                          = 0;
  if (y >= screenHeight)
    y                          = screenHeight - 1;
  if (y < 0)
    y                          = 0;

  /* Directly move cursor by cursor addressing                               */
  if (expandParm2(absolute, cursor_address, y, x))
    absoluteLength             = strlen(absolute);
  else
    absoluteLength             = UNDEF;

  /* Move cursor vertically                                                  */
  if (y == currentBuffer->cursorY) {
    vertical[0]                = '\000';
    verticalLength             = 0;
  } else {
    if (y < currentBuffer->cursorY) {
      if (expandParm(vertical, parm_up_cursor, currentBuffer->cursorY - y))
        verticalLength         = strlen(vertical);
      else
        verticalLength         = UNDEF;
      if (cursor_up && strcmp(cursor_up, "@") &&
          (i = (currentBuffer->cursorY - y) *
               strlen(cursor_up)) < verticalLength &&
          i < absoluteLength &&
          i < sizeof(vertical)) {
        vertical[0]            = '\000';
        for (i = currentBuffer->cursorY - y; i--; )
          strcat(vertical, cursor_up);
        verticalLength         = strlen(vertical);
      }
      if (cursor_home && strcmp(cursor_home, "@") &&
          cursor_down && strcmp(cursor_down, "@") &&
          (i = strlen(cursor_home) +
               strlen(cursor_down)*y) < verticalLength &&
          i < absoluteLength &&
          i < sizeof(vertical)) {
        strcpy(vertical, cursor_home);
        for (i = y; i--; )
          strcat(vertical, cursor_down);
        verticalLength         = strlen(vertical);
        currentBuffer->cursorX = 0;
        jumpedHome             = 1;
      }
    } else {
      if (expandParm(vertical, parm_down_cursor, y - currentBuffer->cursorY))
        verticalLength         = strlen(vertical);
      else
        verticalLength         = UNDEF;
      if (cursor_down && strcmp(cursor_down, "@") &&
          (i = (y - currentBuffer->cursorY) *
               strlen(cursor_down)) < verticalLength &&
          i < absoluteLength &&
          i < sizeof(vertical)) {
        vertical[0]            = '\000';
        for (i = y - currentBuffer->cursorY; i--; )
          strcat(vertical, cursor_down);
        verticalLength         = strlen(vertical);
      }
    }
  }

  /* Move cursor horizontally                                                */
  if (x == currentBuffer->cursorX) {
    horizontal[0]              = '\000';
    horizontalLength           = 0;
  } else {
    if (x < currentBuffer->cursorX) {
      char *cr                 = carriage_return ? carriage_return : "\r";

      if (expandParm(horizontal, parm_left_cursor, currentBuffer->cursorX - x))
        horizontalLength       = strlen(horizontal);
      else
        horizontalLength       = UNDEF;
      if (cursor_left && strcmp(cursor_left, "@") &&
          (i = (currentBuffer->cursorX - x) *
               strlen(cursor_left)) < horizontalLength &&
          i < absoluteLength &&
          i < sizeof(horizontal)) {
        horizontal[0]          = '\000';
        for (i = currentBuffer->cursorX - x; i--; )
          strcat(horizontal, cursor_left);
        horizontalLength       = strlen(horizontal);
      }
      if (cursor_right && strcmp(cursor_right, "@") &&
          (i = strlen(cr) + strlen(cursor_right)*x) < horizontalLength &&
          i < absoluteLength &&
          i < sizeof(horizontal)) {
        strcpy(horizontal, cr);
        for (i = x; i--; )
          strcat(horizontal, cursor_right);
        horizontalLength       = strlen(horizontal);
      }
    } else {
      if (expandParm(horizontal, parm_right_cursor,x - currentBuffer->cursorX))
        horizontalLength       = strlen(horizontal);
      else
        horizontalLength       = UNDEF;
      if (cursor_right && strcmp(cursor_right, "@") &&
         (i = (x - currentBuffer->cursorX) *
              strlen(cursor_right)) < horizontalLength &&
          i < absoluteLength &&
          i < sizeof(horizontal)) {
        horizontal[0]          = '\000';
        for (i = x - currentBuffer->cursorX; i--; )
          strcat(horizontal, cursor_right);
        horizontalLength       = strlen(horizontal);
      }
    }
  }

  /* Move cursor                                                             */
  if (absoluteLength < horizontalLength + verticalLength) {
    if (absoluteLength)
      putCapability(absolute);
  } else {
    if (jumpedHome) {
      if (verticalLength)
        putCapability(vertical);
      if (horizontalLength)
        putCapability(horizontal);
    } else {
      if (horizontalLength)
        putCapability(horizontal);
      if (verticalLength)
        putCapability(vertical);
    }
  }

  currentBuffer->cursorX       = x;
  currentBuffer->cursorY       = y;

  return;
}


static void gotoXYforce(int x, int y) {
  char buffer[1024];

  /* This function gets called when we do not know where the cursor currently*/
  /* is. So, the safest thing is to use absolute cursor addressing (if       */
  /* available) to force the cursor position. Otherwise, we fall back on     */
  /* relative positioning and keep our fingers crossed.                      */
  if (x >= screenWidth)
    x                        = screenWidth - 1;
  if (x < 0)
    x                        = 0;
  if (y >= screenHeight)
    y                        = screenHeight - 1;
  if (y < 0)
    y                        = 0;
  if (expandParm2(buffer, cursor_address, y, x)) {
    putCapability(buffer);
    currentBuffer->cursorX   = x;
    currentBuffer->cursorY   = y;
  } else {
    if (cursor_home && strcmp(cursor_home, "@")) {
      putCapability(cursor_home);
      currentBuffer->cursorX = 0;
      currentBuffer->cursorY = 0;
    }
    gotoXY(x, y);
  }
  return;
}


static void gotoXYscroll(int x, int y) {
  char buffer[1024];

  if (x >= 0 && x < screenWidth) {
    if (y < 0) {
      moveScreenBuffer(currentBuffer,
                       0, 0, screenWidth - 1, screenHeight - 1 + y,
                       0, -y);
      gotoXY(0, 0);
      if (parm_insert_line && strcmp(parm_insert_line, "@")) {
        putCapability(expandParm(buffer, parm_insert_line, -y));
      } else {
        while (y++ < 0)
          putCapability(insert_line);
      }
      gotoXY(x, 0);
    } else if (y >= screenHeight) {
      moveScreenBuffer(currentBuffer,
                       0, y - screenHeight + 1,
                       screenWidth - 1, screenHeight - 1,
                       0, screenHeight - y - 1);
      if (scroll_forward && strcmp(scroll_forward, "@")) {
        gotoXY(screenWidth - 1, screenHeight - 1);
        while (y-- >= screenHeight)
          putCapability(scroll_forward);
      } else {
        gotoXY(0,0);
        if (parm_delete_line && strcmp(parm_delete_line, "@")) {
          putCapability(expandParm(buffer, parm_delete_line,
                                   y - screenHeight + 1));
        } else {
          while (y-- >= screenHeight)
            putCapability(delete_line);
        }
      }
      gotoXYforce(x, screenHeight - 1);
    } else
      gotoXY(x, y);
  }
  return;
}


static void clearEol(void) {
  clearExcessBuffers();
  if (writeProtection) {
    int x                        = currentBuffer->cursorX;
    int y                        = currentBuffer->cursorY;
    unsigned short *attributePtr = currentBuffer->attributes[y];
    char *charPtr                = currentBuffer->lineBuffer[y];
    for (; x < screenWidth &&  (attributePtr[x] & T_PROTECTED); x++);
    for (; x < screenWidth && !(attributePtr[x] & T_PROTECTED); x++) {
      attributePtr[x]            = T_NORMAL;
      charPtr[x]                 = ' ';
    }
    displayCurrentScreenBuffer();
  } else {
    clearScreenBuffer(currentBuffer,
                      currentBuffer->cursorX, currentBuffer->cursorY,
                      screenWidth-1, currentBuffer->cursorY,
                      T_NORMAL, ' ');
    if (clr_eol && strcmp(clr_eol, "@")) {
      putCapability(clr_eol);
    } else {
      int oldX                   = currentBuffer->cursorX;
      int oldY                   = currentBuffer->cursorY;
      int i;
  
      for (i = oldX; i < screenWidth-1; i++)
        putConsole(' ');
      if (insert_character && strcmp(insert_character, "@"))
        putCapability(insert_character);
      else {
        if (!insertMode && enter_insert_mode && strcmp(enter_insert_mode, "@"))
          putCapability(enter_insert_mode);
        putConsole(' ');
        if (!insertMode && exit_insert_mode && strcmp(exit_insert_mode, "@"))
          putCapability(exit_insert_mode);
      }
      gotoXYforce(oldX, oldY);
    }
  }
  return;
}


static void clearEos(void) {
  clearExcessBuffers();
  if (writeProtection) {
    int x                          = currentBuffer->cursorX;
    int y                          = currentBuffer->cursorY;
    for (; y < screenHeight; y++, x = 0) {
      unsigned short *attributePtr = &currentBuffer->attributes[y][x];
      char *charPtr                = &currentBuffer->lineBuffer[y][x];
      for (; x < screenWidth; x++) {
        if (!(*attributePtr & T_PROTECTED)) {
          *attributePtr            = T_NORMAL;
          *charPtr                 = ' ';
        }
        attributePtr++;
        charPtr++;
      }
    }
    displayCurrentScreenBuffer();
  } else if (clr_eos && strcmp(clr_eos, "@")) {
    clearScreenBuffer(currentBuffer,
                      currentBuffer->cursorX, currentBuffer->cursorY,
                      screenWidth-1, currentBuffer->cursorY, T_NORMAL, ' ');
    clearScreenBuffer(currentBuffer,
                      0, currentBuffer->cursorY+1,
                      screenWidth-1, screenHeight-1, T_NORMAL, ' ');
    putCapability(clr_eos);
  } else {
    int oldX                       = currentBuffer->cursorX;
    int oldY                       = currentBuffer->cursorY;
    int i;

    for (i = oldY; i < screenHeight; i++) {
      if (i > oldY)
        gotoXYforce(0, i);
      clearEol();
    }
    gotoXYforce(oldX, oldY);
  }
  return;
}


static void fillScreen(unsigned short attributes, const char fillChar) {
  clearExcessBuffers();
  if (writeProtection) {
    int x, y;
    int foundHome                  = 0;

    for (y = 0; y < screenHeight; y++) {
      char *charPtr                = currentBuffer->lineBuffer[y];
      unsigned short *attributePtr = currentBuffer->attributes[y];
      for (x = 0; x < screenWidth; x++) {
        if (*attributePtr++ & T_PROTECTED) {
          charPtr++;
        } else {
          if (!foundHome) {
            foundHome++;
            currentBuffer->cursorX = x;
            currentBuffer->cursorY = y;
          }
          *charPtr++               = fillChar;
          attributePtr[-1]         = attributes;
        }
      }
      displayCurrentScreenBuffer();
    }
  } else if (attributes != T_NORMAL || fillChar != ' ') {
    clearScreenBuffer(currentBuffer, 0, 0, screenWidth-1, screenHeight-1,
                      attributes, fillChar);
    currentBuffer->cursorX         = 0;
    currentBuffer->cursorY         = 0;
    displayCurrentScreenBuffer();
  } else {
    if (clear_screen && strcmp(clear_screen, "@")) {
      clearScreenBuffer(currentBuffer, 0, 0, screenWidth-1, screenHeight-1,
                        attributes, fillChar);
      putCapability(clear_screen);
    } else {
      gotoXYforce(0, 0);
      clearEos();
    }
    currentBuffer->cursorX         = 0;
    currentBuffer->cursorY         = 0;
  }
  return;
}


static void clearScreen() {
  fillScreen(T_NORMAL, ' ');
  return;
}


static void setPage(int page) {
  if (page < 0)
    page                      = 0;
  else if (page > 2)
    page                      = 2;
  if (page != currentPage) {
    clearExcessBuffers();
    if (page && !currentPage) {
      if (enter_ca_mode && strcmp(enter_ca_mode, "@"))
        putCapability(enter_ca_mode);
    } else if (!page && currentPage) {
      if (exit_ca_mode && strcmp(exit_ca_mode, "@"))
        putCapability(exit_ca_mode);
    }
    currentPage              = page;
    currentBuffer            = screenBuffer[page];
    displayCurrentScreenBuffer();
  }
  return;
}


static void putGraphics(char ch) {
  static void updateAttributes(void);

  if (ch == '\x02')
    graphicsMode                  = 1;
  else if (ch == '\x03')
    graphicsMode                  = 0;
  else if (ch >= '0' && ch <= '?') {
    if (acs_chars &&
        enter_alt_charset_mode && strcmp(enter_alt_charset_mode, "@")) {
      static const char map[]     = "wmlktjx0nuqaqvxa";
      char              *ptr;
      int               cursorX   = currentBuffer->cursorX;
      int               cursorY   = currentBuffer->cursorY;

      if (cursorX >= 0 && cursorY >= 0 &&
          cursorX < screenWidth && cursorY < screenHeight) {
        unsigned short attributes = (unsigned short)currentAttributes |
                                    T_GRAPHICS;
    
        currentBuffer->lineBuffer[cursorY][cursorX] = ch;
        if (protected)
          attributes             |= T_PROTECTED;
        currentBuffer->attributes[cursorY][cursorX] = attributes;
      }
      ch                      = map[ch - '0'];
      for (ptr = acs_chars; ptr[0] && ptr[1] && *ptr != ch; ptr += 2);
      if (*ptr) {
        char buffer[2];

        buffer[0]             = ptr[1];
        buffer[1]             = '\000';
        putCapability(enter_alt_charset_mode);
        putCapability(buffer);
        putCapability(exit_alt_charset_mode);
      } else {
        if (ch == '0' || ch == 'a' || ch == 'h') {
          if (currentAttributes & T_REVERSE) {
            if (exit_standout_mode && strcmp(exit_standout_mode, "@"))
              putCapability(exit_standout_mode);
          } else {
            if (enter_standout_mode && strcmp(enter_standout_mode, "@"))
              putCapability(enter_standout_mode);
          }
          _putConsole(' ');
          currentAttributes   = -1;
          updateAttributes();
        } else
          _putConsole(' ');
      }
    } else {
      putConsole(' ');
    }
  }
  return;
}


static void showCursor(int flag) {
  if (!cursorIsHidden != flag) {
    if (flag) {
      if (cursor_visible && strcmp(cursor_visible, "@"))
        putCapability(cursor_visible);
      if (cursor_normal && strcmp(cursor_normal, "@"))
        putCapability(cursor_normal);
    } else {
      if (cursor_invisible && strcmp(cursor_invisible, "@"))
        putCapability(cursor_invisible);
    }
    cursorIsHidden = !flag;
  }
  return;
}


static void executeExternalProgram(char *argv[]) {
  static void failure(int exitCode, const char *message, ...);
  int    pid, status;

  if ((pid = fork()) < 0) {
    return;
  } else if (pid == 0) {
    /* In child process                                                      */
    char linesEnvironment[80];
    char columnsEnvironment[80];
    int  i;

    /* Close all file handles                                                */
    closelog();
    for (i           = sysconf(_SC_OPEN_MAX); --i > 2;)
      close(i);

    /* Close stdin and stderr so that the external script cannot accidentally*/
    /* interfere with what is shown on the screen; but leave stdout open     */
    /* because it might have to write to the screen to reset or resize it.   */
    close(0);
    close(2);

    /* Configure environment variables                                       */
    snprintf(linesEnvironment,   sizeof(linesEnvironment),
             "LINES=%d",   screenHeight);
    snprintf(columnsEnvironment, sizeof(columnsEnvironment),
             "COLUMNS=%d", screenWidth);
    putenv(linesEnvironment);
    putenv(columnsEnvironment);

#if HAVE_UNSETENV
    unsetenv("IFS");
#endif

    execv(argv[0], argv);
    failure(127, "Could not execute \"%s\"\n", argv[0]);
  } else {
    /* In parent process                                                     */
    waitpid(pid, &status, 0);
  }
  return;
}


static void requestNewGeometry(int pty, int width, int height) {
  static void processSignal(int signalNumber, int pid, int pty);

  logDecode("setScreenSize(%d,%d)", width, height);

  if (screenWidth != width || screenHeight != height) {
    int triedToChange             = 0;

    if (cfgResize && *cfgResize) {
      char widthBuffer[80];
      char heightBuffer[80];
      char *argv[4];

      argv[0]                     = cfgResize;
      argv[1]                     = widthBuffer;
      argv[2]                     = heightBuffer;
      argv[3]                     = NULL;

      triedToChange               = 1;
      sprintf(widthBuffer,  "%d", width);
      sprintf(heightBuffer, "%d", height);
      executeExternalProgram(argv);
    }

    if (vtStyleCursorReporting) {
      /* This only works for recent xterm terminals, but it gets silently    */
      /* ignored if used on any ANSI style terminal.                         */
      char buffer[20];

      triedToChange               = 1;
      sprintf(buffer, "\x1B[8;%d;%dt", height, width);
      putCapability(buffer);
      flushConsole();
    }

    changedDimensions            |= triedToChange;

    if (triedToChange && pty >= 0) {
      /* If we can wait until the screen has actually resized, then output   */
      /* will be a lot more accurate. Unfortunately, we don't know whether   */
      /* the underlying terminal understands about resizing; so we also      */
      /* have to time out after a little while.                              */
      struct itimerval timer;

      useAuxiliarySignalHandler   = 1;
      switch (sigsetjmp(auxiliaryJumpBuffer, 1)) {
      case SIGWINCH:
        processSignal(SIGWINCH, -1, pty);
        break;
      case SIGALRM:
        break;
      case 0: {
        sigset_t         signals, oldSignals;
        
        sigemptyset(&signals);
        sigprocmask(SIG_BLOCK, &signals, &signals);
        sigdelset(&signals, SIGALRM);
        sigdelset(&signals, SIGWINCH);
        sigprocmask(SIG_SETMASK, &signals, &oldSignals);
        timer.it_interval.tv_sec  = 0;
        timer.it_interval.tv_usec = 0;
        timer.it_value.tv_sec     = 1;
        timer.it_value.tv_usec    = 0;
        if (!setitimer(ITIMER_REAL, &timer, NULL))
          sigsuspend(&signals);
        sigprocmask(SIG_SETMASK, &oldSignals, NULL);
        break; }
      default:
        break;
      }
      timer.it_interval.tv_sec    = 0;
      timer.it_interval.tv_usec   = 0;
      timer.it_value.tv_sec       = 0;
      timer.it_value.tv_usec      = 0;
      setitimer(ITIMER_REAL, &timer, NULL);
      useAuxiliarySignalHandler   = 0;
    }
  }

  /* The nominal screen geometry is the one that was requested by the        */
  /* application; it might differ from the real dimensions if the user       */
  /* manually resized the screen.                                            */
  nominalWidth                    = width;
  nominalHeight                   = height;

  return;
}


static void sendResetStrings(void) {
  char buffer[1024];

  if (init_prog && strcmp(init_prog, "@")) {
    char *argv[2];
    
    argv[0]      = init_prog;
    argv[1]      = NULL;

    executeExternalProgram(argv);
  }

#ifdef RESET_TERMINAL
  if (reset_1string && strcmp(reset_1string, "@"))
    putCapability(reset_1string);
  else
#endif
  if (init_1string && strcmp(init_1string, "@"))
    putCapability(init_1string);

#ifdef RESET_TERMINAL
  if (reset_2string && strcmp(reset_2string, "@"))
    putCapability(reset_2string);
  else
#endif
  if (init_2string && strcmp(init_2string, "@"))
    putCapability(init_2string);

  if (reset_file || init_file) {
    int fd       = -1;

#ifdef RESET_TERMINAL
    if (reset_file && strcmp(reset_file, "@"))
      fd         = open(reset_file, O_RDONLY);
#endif
    if (fd < 0 && init_file && strcmp(init_file, "@"))
      fd         = open(init_file, O_RDONLY);

    if (fd >= 0) {
      int len;
      while ((len= read(fd, buffer, sizeof(buffer))) > 0)
        writeConsole(buffer, len);
      close(fd);
    }
  }

#ifdef RESET_TERMINAL
  if (reset_3string && strcmp(reset_3string, "@"))
    putCapability(reset_3string);
  else
#endif
  if (init_3string && strcmp(init_3string, "@"))
    putCapability(init_3string);

  return;
}


static void _resetTerminal(int resetSize) {
  flushConsole();

  if (needsReset) {
    needsReset = 0;

    showCursor(1);
    sendResetStrings();
    reset_shell_mode();

    flushConsole();

    /* Reset the terminal dimensions if we changed them programatically      */
    if (changedDimensions && resetSize) {
      requestNewGeometry(-1, originalWidth, originalHeight);
    }

    tcsetattr(0, TCSANOW, &defaultTermios);
    tcsetattr(1, TCSANOW, &defaultTermios);
    tcsetattr(2, TCSANOW, &defaultTermios);
  }
  return;
}


static void resetTerminal() {
  _resetTerminal(1);
  return;
}


static void failure(int exitCode, const char *message, ...) {
  va_list argList;
  char    buffer[1024];

  resetTerminal();
  va_start(argList, message);
  strcpy(buffer, "\n");
  vsnprintf(buffer, sizeof(buffer) - 2, message, argList);
  strcat(buffer, "\r\n");
  va_end(argList);
  write(2, buffer, strlen(buffer));
  exit(exitCode);
}


static char *readResponse(int timeout, const char *query, char *buffer,
                          int firstChar, int lastChar, int discard,
                          int maxLength) {
  struct pollfd descriptors[1];
  char          *ptr    = buffer;
  int           i, j, count;
  int           state   = 0;

  descriptors[0].fd     = 0;
  descriptors[0].events = POLLIN;

  do {
    switch (poll(descriptors, 1, 0)) {
    case -1:
    case 0:
      i                 = 0;
      break;
    default:
      i                 = sizeof(extraData) - extraDataLength;
      if (i == 0) {
        *buffer         = '\000';
        return(buffer);
      } else if ((count = read(0, extraData + extraDataLength, i)) > 0) {
        extraDataLength+= count;
      } else
        i               = 0;
    }
  } while (i);

  writeConsole(query, strlen(query));
  flushConsole();

  while (state != 2) {
    switch (poll(descriptors, 1, timeout)) {
    case -1:
    case 0:
      state             = 2;
      break;
    default:
      i                 = sizeof(extraData) - extraDataLength;
      if (i == 0) {
        *buffer         = '\000';
        return(buffer);
      }
      if ((count        = read(0, extraData + extraDataLength, i)) > 0) {
        j               = 0;
        for (i = extraDataLength; i < extraDataLength + count; i++) {
          if (state == 0) {
            if (firstChar < 0 || (((int)extraData[i])&0xFF) == firstChar) {
              state     = 1;
            } else
              j++;
          }
          if (state == 1) {
            if (maxLength-- > 1)
              *ptr++    = extraData[i];
            if (extraData[i] == lastChar) {
              i++;
              state     = 2;
              break;
            }
          }
        }
        for (; i < extraDataLength + count &&
               discard >= 0 &&
               (((int)extraData[i])&0xFF) == discard;
             i++);
        memmove(extraData + extraDataLength + j,
                extraData + i,
                extraDataLength + count - i);
        extraDataLength+= j + extraDataLength + count - i;
      }
      break;
    }
  }
  *ptr                  = '\000';
  return(buffer);
}


static int queryCursorPosition(int *x, int *y) {
  char buffer[80], *ptr;

  if (vtStyleCursorReporting) {
    readResponse(1000, "\x1B[6n", buffer, '\x1B', 'R', '\000', sizeof(buffer));
    if (*buffer != '\000') {
      ptr      = strchr(buffer, ';');
      if (ptr != NULL) {
        *ptr++ = '\000';
        *x     = atoi(ptr) - 1;
        *y     = atoi(buffer+2) - 1;
        return(1);
      }
    }
  } else if (wyStyleCursorReporting) {
    readResponse(1000, "\x1B""b", buffer, -1, 'C', '\r', sizeof(buffer));
    if (*buffer != '\000') {
      ptr      = strchr(buffer, 'R');
      if (ptr != NULL) {
        *ptr++ = '\000';
        *x     = atoi(ptr)    - 1;
        *y     = atoi(buffer) - 1;
        return(1);
      }
    }
  }
  return(0);
}


static void addKeyboardTranslation(const char *name,
                                   const char *nativeKeys,
                                   const char *wy60Keys) {
  KeyDefs **definitionPtr            = &keyDefinitions;
  const char *keys                   = "";

  if (nativeKeys && wy60Keys) {
    while (*nativeKeys) {
      while (*definitionPtr != NULL) {
        if ((*definitionPtr)->ch == *nativeKeys) {
          break;
        } else if ((*definitionPtr)->ch > *nativeKeys)
          definitionPtr              = &(*definitionPtr)->left;
        else
          definitionPtr              = &(*definitionPtr)->right;
      }
      if (*definitionPtr == NULL) {
        *definitionPtr               = calloc(sizeof(KeyDefs), 1);
        (*definitionPtr)->ch         = *nativeKeys;
        keys                         = strcpy(calloc(strlen(keys) + 2,1),keys);
        *strrchr(keys, '\000')       = *nativeKeys;
        (*definitionPtr)->nativeKeys = keys;
      } else
        keys                         = (*definitionPtr)->nativeKeys;
      if (*++nativeKeys) {
        definitionPtr                = &(*definitionPtr)->down;
      }
    }
    if (*definitionPtr != NULL && (*definitionPtr)->name == NULL) {
      (*definitionPtr)->name         = name;
      (*definitionPtr)->wy60Keys     = wy60Keys;
    }
  }
  return;
}


static void userInputReceived(int pty, const char *buffer, int count) {
  int i;

  for (i = 0; i < count; i++) {
    char ch                  = buffer[i];

    logHostKey(ch);
    if (currentKeySequence == NULL)
      currentKeySequence     = keyDefinitions;

    for (;;) {
      if (currentKeySequence->ch == ch) {
        if (currentKeySequence->down == NULL) {
          /* Found a match. Translate key sequence now.                      */
          logCharacters(0, currentKeySequence->wy60Keys,
                        strlen(currentKeySequence->wy60Keys));
          write(pty, currentKeySequence->wy60Keys,
                strlen(currentKeySequence->wy60Keys));
          currentKeySequence = NULL;
          break;
        } else {
          currentKeySequence = currentKeySequence->down;
          break;
        }
      } else if (currentKeySequence->ch > ch) {
        if (currentKeySequence->left == NULL) {
          /* Sequence is not known. Output verbatim.                         */
          int length;
        noTranslation:
          length             = strlen(currentKeySequence->nativeKeys);
          if (length > 1) {
            logCharacters(0, currentKeySequence->nativeKeys, length-1);
            logCharacters(0, &ch, 1);
            write(pty, currentKeySequence->nativeKeys, length-1);
          }
          write(pty, &ch, 1);
          currentKeySequence = NULL;
          break;
        } else {
          /* Traverse left sub-tree                                          */
          currentKeySequence = currentKeySequence->left;
        }
      } else {
        if (currentKeySequence->right == NULL) {
          goto noTranslation;
        } else {
          /* Traverse right sub-tree                                         */
          currentKeySequence = currentKeySequence->right;
        }
      }
    }
  }

  return;
}


static void updateAttributes(void) {
  int attributes;

  if (protected) {
    attributes            = normalAttributes | protectedAttributes;
  } else
    attributes            = normalAttributes;

  if (attributes != currentAttributes) {
    char buffer[1024];

    /* Show different combinations of attributes by using different ANSI     */
    /* colors                                                                */
    if (set_a_foreground && strcmp(set_a_foreground, "@")) {
      int color           = ((attributes & T_BLINK)      ? 1 : 0) +
                            ((attributes & T_UNDERSCORE) ? 2 : 0) +
                            ((attributes & T_DIM)        ? 4 : 0);

      if (currentAttributes & T_REVERSE)
        if (exit_standout_mode && strcmp(exit_standout_mode, "@"))
          putCapability(exit_standout_mode);
      if (!(orig_pair && strcmp(orig_pair, "@")) || color) {
        if (!color)
          color           = 9; /* reset color to default value */
        else if (color == 7)
          color           = 6; /* white does not display on white background */
        putCapability(expandParm(buffer, set_a_foreground, color));
      } else
        putCapability(orig_pair);
      if (attributes & T_REVERSE)
        if (enter_standout_mode && strcmp(enter_standout_mode, "@"))
          putCapability(enter_standout_mode);

      /* Terminal supports non-ANSI colors (probably in the range 0..7)      */
    } else if (set_foreground && strcmp(set_foreground, "@") &&
               orig_pair      && strcmp(orig_pair,      "@")) {
      int color           = ((attributes & T_BLINK)      ? 1 : 0) +
                            ((attributes & T_UNDERSCORE) ? 2 : 0) +
                            ((attributes & T_DIM)        ? 4 : 0);

      if (currentAttributes & T_REVERSE)
        if (exit_standout_mode && strcmp(exit_standout_mode, "@"))
          putCapability(exit_standout_mode);
      if (color) {
        if (color == 7)
          color           = 6; /* white does not display on white background */
        putCapability(expandParm(buffer, set_foreground, color));
      } else
        putCapability(orig_pair);
      if (attributes & T_REVERSE)
        if (enter_standout_mode && strcmp(enter_standout_mode, "@"))
          putCapability(enter_standout_mode);

      /* Terminal doesn't support colors, but can set multiple attributes at */
      /* once                                                                */
    } else if (expandParm9(buffer, set_attributes,
                   0,
                   !!(attributes & T_UNDERSCORE),
                   (attributes & T_REVERSE) &&
                   (!(attributes & T_DIM) || !protected),
                   !!(attributes & T_BLINK),
                   (attributes & T_DIM) &&
                   (!(attributes & T_REVERSE) || !protected),
                   (attributes & T_BOTH) == T_BOTH && protected,
                   0, 0, 0)) {
      putCapability(buffer);

      /* Terminal can only set some attributes. It might or might not        */
      /* support combinations of attributes.                                 */
    } else {
      int isBoth           = 0;

      if (exit_attribute_mode && strcmp(exit_attribute_mode, "@"))
        putCapability(exit_attribute_mode);
      else {
        if (currentAttributes & (T_DIM | T_UNDERSCORE))
          if (exit_underline_mode && strcmp(exit_underline_mode, "@"))
            putCapability(exit_underline_mode);
        if (currentAttributes & T_REVERSE)
          if (exit_standout_mode && strcmp(exit_standout_mode, "@"))
            putCapability(exit_standout_mode);
      }
      if ((attributes & T_BOTH) == T_BOTH &&
          exit_attribute_mode && strcmp(exit_attribute_mode, "@") &&
          enter_bold_mode     && strcmp(enter_bold_mode,     "@")) {
        putCapability(enter_bold_mode);
        isBoth            = 1;
      }
      if (attributes & T_BLINK &&
          exit_attribute_mode && strcmp(exit_attribute_mode, "@") &&
          enter_blink_mode    && strcmp(enter_blink_mode,    "@"))
        putCapability(enter_blink_mode);
      if (attributes & T_UNDERSCORE &&
          enter_underline_mode && strcmp(enter_underline_mode, "@"))
        putCapability(enter_underline_mode);
      if ((attributes & T_DIM) && !isBoth) {
        if (exit_attribute_mode && strcmp(exit_attribute_mode, "@") &&
            enter_dim_mode      && strcmp(enter_dim_mode,      "@"))
          putCapability(enter_dim_mode);
        else if (enter_underline_mode && strcmp(enter_underline_mode, "@") &&
                 !(attributes & T_UNDERSCORE))
          putCapability(enter_underline_mode);
      }
      if ((attributes & T_REVERSE) && !isBoth)
        if (enter_standout_mode && strcmp(enter_standout_mode, "@"))
          putCapability(enter_standout_mode);
    }

    currentAttributes     = attributes;
  }
  return;
}


static void setFeatures(int attributes) {
  attributes           &= T_ALL;
  protectedPersonality  = attributes;
  updateAttributes();
  return;
}


static void setAttributes(int attributes) {
  attributes           &= T_ALL;
  if (protected)
    protectedAttributes = attributes | protectedPersonality;
  else
    normalAttributes    = attributes;
  updateAttributes();
  return;
}


static void setProtected(int flag) {
  protected = flag;
  updateAttributes();
  return;
}


static void setWriteProtection(int flag) {
  writeProtection = flag;
  return;
}


static void escape(int pty,char ch) {
  mode           = E_NORMAL;
  switch (ch) {
  case ' ': /* Reports the terminal identification                           */
    logDecode("sendTerminalId()");
    sendUserInput(pty, "60\r", 3);
    break;
  case '!': /* Writes all unprotected character positions with an attribute  */
    /* not supported: I don't understand this command */
    logDecode("NOT SUPPORTED [ 0x1B 0x21");
    mode         = E_SKIP_ONE;
    break;
  case '\"':/* Unlocks the keyboard                                          */
    /* not supported: keyboard locking */
    logDecode("unlockKeyboard() /* NOT SUPPORTED */");
    break;
  case '#': /* Locks the keyboard                                            */
    /* not supported: keyboard locking */
    logDecode("unlockKeyboard() /* NOT SUPPORTED */");
    break;
  case '&': /* Turns the protect submode on and prevents auto scroll         */
    logDecode("enableProtected() ");
    setWriteProtection(1);
    break;
  case '\'':/* Turns the protect submode off and allows auto scroll          */
    logDecode("disableProtected() ");
    setWriteProtection(0);
    break;
  case '(': /* Turns the write protect submode off                           */
    logDecode("disableProtected()");
    setProtected(0);
    break;
  case ')': /* Turns the write protect submode on                            */
    logDecode("enableProtected()");
    setProtected(1);
    break;
  case '*': /* Clears the screen to nulls; protect submode is turned off     */
    logDecode("disableProtected() ");
    logDecode("clearScreen()");
    setProtected(0);
    setWriteProtection(0);
    clearScreen();
    break;
  case '+': /* Clears the screen to spaces; protect submode is turned off    */
    logDecode("disableProtected() ");
    logDecode("clearScreen()");
    setProtected(0);
    setWriteProtection(0);
    clearScreen();
    break;
  case ',': /* Clears screen to protected spaces; protect submode is turned  */
            /* off                                                           */
    logDecode("disableProtected() ");
    logDecode("clearScreen()");
    setProtected(1);
    setWriteProtection(0);
    fillScreen(T_PROTECTED | protectedPersonality, ' ');
    break;
  case '-': /* Moves cursor to a specified text segment                      */
    /* not supported: text segments */
    logDecode("NOT SUPPORTED [ 0x1B 0x2D ] ");
    mode         = E_GOTO_SEGMENT;
    break;
  case '.': /* Clears all unprotected characters positions with a character  */
    mode         = E_FILL_SCREEN;
    break;
  case '/':{/* Transmits the active text segment number and cursor address   */
    /* not supported: text segments */
    char buffer[4];
    logDecode("sendCursorAddress()");
    buffer[0]    = ' ';
    buffer[1]    = (char)(currentBuffer->cursorY + 32);
    buffer[2]    = (char)(currentBuffer->cursorX + 32);
    buffer[3]    = '\r';
    sendUserInput(pty, buffer, 4);
    break; }
  case '0': /* Clears all tab settings                                       */
    /* not supported: tab stops */
    logDecode("clearAllTabStops() /* NOT SUPPORTED */");
    break;
  case '1': /* Sets a tab stop                                               */
    /* not supported: tab stops */
    logDecode("setTabStop() /* NOT SUPPORTED */");
    break;
  case '2': /* Clears a tab stop                                             */
    /* not supported: tab stops */
    logDecode("clearTabStop() /* NOT SUPPORTED */");
    break;
  case '4': /* Sends all unprotected characters from the start of row to host*/
    /* not supported: screen sending */
    logDecode("sendAllUnprotectedCharactersFromStartOfRow() "
              "/* NOT SUPPORTED */");
    break;
  case '5': /* Sends all unprotected characters from the start of text to    */
            /*  host                                                         */
    /* not supported: screen sending */
    logDecode("sendAllUnprotectedCharacters() /* NOT SUPPORTED */");
    break;
  case '6': /* Sends all characters from the start of row to the host        */
    /* not supported: screen sending */
    logDecode("sendAllCharactersFromStartOfRow() /* NOT SUPPORTED */");
    break;
  case '7': /* Sends all characters from the start of text to the host       */
    /* not supported: screen sending */
    logDecode("sendAllCharacters() /* NOT SUPPORTED */");
    break;
  case '8': /* Enters a start of message character (STX)                     */
    /* not supported: unknown */
    logDecode("enterSTX() /* NOT SUPPORTED */");
    break;
  case '9': /* Enters an end of message character (ETX)                      */
    /* not supported: unknown */
    logDecode("enterETX() /* NOT SUPPORTED */");
    break;
  case ':': /* Clears all unprotected characters to null                     */
    logDecode("clearScreen()");
    clearScreen();
    break;
  case ';': /* Clears all unprotected characters to spaces                   */
    logDecode("clearScreen()");
    clearScreen();
    break;
  case '=': /* Moves cursor to a specified row and column                    */
    mode         = E_GOTO_ROW_CODE;
    break;
  case '?':{/* Transmits the cursor address for the active text segment      */
    char buffer[3];
    logDecode("sendCursorAddress()");
    buffer[0]    = (char)(currentBuffer->cursorY + 32);
    buffer[1]    = (char)(currentBuffer->cursorX + 32);
    buffer[2]    = '\r';
    sendUserInput(pty, buffer, 3);
    break; }
  case '@': /* Sends all unprotected characters from start of text to aux    */
            /* port                                                          */
    /* not supported: auxiliary port */
    logDecode("sendAllUnprotectedCharactersToAux() /* NOT SUPPORTED */");
    break;
  case 'A': /* Sets the video attributes                                     */
    mode         = E_SET_FIELD_ATTRIBUTE;
    break;
  case 'B': /* Places the terminal in block mode                             */
    /* not supported: block mode */
    logDecode("enableBlockMode() /* NOT SUPPORTED */");
    break;
  case 'C': /* Places the terminal in conversation mode                      */
    /* not supported: block mode */
    logDecode("enableConversationMode() /* NOT SUPPORTED */");
    break;
  case 'D': /* Sets full of half duplex conversation mode                    */
    /* not supported: block mode */
    logDecode("enableConversationMode() /* NOT SUPPORTED */ [");
    mode         = E_SKIP_ONE;
    break;
  case 'E': /* Inserts a row of spaces                                       */
    logDecode("insertLine()");
    moveScreenBuffer(currentBuffer,
                     0, currentBuffer->cursorY,
                     screenWidth - 1, screenHeight - 1,
                     0, 1);
    if (insert_line && strcmp(insert_line, "@"))
      putCapability(insert_line);
    else {
      char buffer[1024];

      putCapability(expandParm(buffer, parm_insert_line, 1));
    }
    break;
  case 'F': /* Enters a message in the host message field                    */
    /* not supported: messages */
    logDecode("enterMessage() [");
    mode         = E_SKIP_LINE;
    break;
  case 'G': /* Sets a video attributes                                       */
    mode         = E_SET_ATTRIBUTE;
    break;
  case 'H': /* Enters a graphic character at the cursor position             */
    /* not supported: graphic characters */
    mode         = E_GRAPHICS_CHARACTER;
    break;
  case 'I': /* Moves cursor left to previous tab stop                        */
    logDecode("backTab()");
    gotoXY((currentBuffer->cursorX - 1) & ~7, currentBuffer->cursorY);
    break;
  case 'J': /* Display previous page                                         */
    logDecode("displayPreviousPage()");
    setPage(currentPage - 1);
    break;
  case 'K': /* Display next page                                             */
    logDecode("displayNextPage()");
    setPage(currentPage + 1);
    break;
  case 'L': /* Sends all characters unformatted to auxiliary port            */
    /* not supported: screen sending  */
    logDecode("sendAllCharactersToAux() /* NOT SUPPORTED */");
    break;
  case 'M': /* Transmit character at cursor position to host                 */
    /* not supported: screen sending */
    logDecode("sendCharacter() /* NOT SUPPORTED */");
    sendUserInput(pty, "\000", 1);
    break;
  case 'N': /* Turns no-scroll submode on                                    */
    /* not supported: scroll mode */
    logDecode("enableNoScrollMode() /* NOT SUPPORTED */");
    break;
  case 'O': /* Turns no-scroll submode off                                   */
    /* not supported: scroll mode */
    logDecode("disableNoScrollMode() /* NOT SUPPORTED */");
    break;
  case 'P': /* Sends all protected and unprotected characters to the aux port*/
    /* not supported: screen sending */
    logDecode("sendAllCharactersToAux() /* NOT SUPPORTED */");
    break;
  case 'Q': /* Inserts a character                                           */
    logDecode("insertCharacter()");
    _moveScreenBuffer(currentBuffer,
                      currentBuffer->cursorX, currentBuffer->cursorY,
                      screenWidth - 1, currentBuffer->cursorY,
                      1, 0);
    if (insert_character && strcmp(insert_character, "@"))
      putCapability(insert_character);
    else {
      putCapability(enter_insert_mode);
      putConsole(' ');
      gotoXY(currentBuffer->cursorX, currentBuffer->cursorY);
    }
    break;
  case 'R': /* Deletes a row                                                 */
    logDecode("deleteLine()");
    moveScreenBuffer(currentBuffer,
                     0, currentBuffer->cursorY + 1,
                     screenWidth - 1, screenHeight - 1,
                     0, 1);
    putCapability(delete_line);
    break;
  case 'S': /* Sends a message unprotected                                   */
    /* not supported: messages */
    logDecode("sendMessage() /* NOT SUPPORTED */");
    break;
  case 'T': /* Erases all characters                                         */
    logDecode("clearToEndOfLine()");
    clearEol();
    break;
  case 'U': /* Turns the monitor submode on                                  */
    /* not supported: monitor mode */
    logDecode("enableMonitorMode() /* NOT SUPPORTED */");
    break;
  case 'V':{/* Sets a protected column                                       */
    int x, y;
    x            = currentBuffer->cursorX;
    for (y = 0; y < screenHeight; y++) {
      currentBuffer->attributes[y][x]  = T_PROTECTED | protectedPersonality;
      currentBuffer->lineBuffer[y][x]  = ' ';
    }
    displayCurrentScreenBuffer();
    break; }
  case 'W': /* Deletes a character                                           */
    logDecode("deleteCharacter()");
    _moveScreenBuffer(currentBuffer,
                      currentBuffer->cursorX + 1, currentBuffer->cursorY,
                      screenWidth - 1, currentBuffer->cursorY,
                      -1, 0);
    putCapability(delete_character);
    break;
  case 'X': /* Turns the monitor submode off                                 */
    /* not supported: monitor mode */
    logDecode("disableMonitorMode() /* NOT SUPPORTED */");
    break;
  case 'Y': /* Erases all characters to the end of the active text segment   */
    /* not supported: text segments */
    logDecode("clearToEndOfSegment() /* NOT SUPPORTED */");
    clearEos();
    break;
  case 'Z': /* Program function key sequence                                 */
    mode         = E_FUNCTION_KEY;
    break;
  case ']': /* Activates text segment zero                                   */
    /* not supported: text segments */
    logDecode("activateSegment(0) /* NOT SUPPORTED */");
    break;
  case '^': /* Select normal or reverse display                              */
    /* not supported: inverting the entire screen */
    logDecode("invertScreen() /* NOT SUPPORTED */ [");
    mode         = E_SKIP_ONE;
    break;
  case '`': /* Sets the screen features                                      */
    mode         = E_SET_FEATURES;
    break;
  case 'a': /* Moves the cursor to a specified row and column                */
    mode         = E_GOTO_ROW;
    targetColumn =
    targetRow    = 0;
    break;
  case 'b':{/* Transmits the cursor address to the host                      */
    char buffer[80];
    logDecode("sendCursorAddress()");
    sprintf(buffer, "%dR%dC",
            currentBuffer->cursorY+1, currentBuffer->cursorX+1);
    sendUserInput(pty, buffer, strlen(buffer));
    break; }
  case 'c': /* Set advanced parameters                                       */
    /* not supported: advanced parameters */
    logDecode("setAdvancedParameters() /* NOT SUPPORTED */ [");
    mode         = E_SKIP_ONE;
    break;
  case 'd': /* Select line wrap mode                                         */
    /* not supported: line wrap mode */
    logDecode("enableLineWrapMode() /* NOT SUPPORTED */");
    mode         = E_SKIP_ONE;
    break;
  case 'e': /* Set communication mode                                        */
    /* not supported: communication modes */
    mode         = E_CSI_E;
    break;
  case 'i': /* Moves the cursor to the next tab stop on the right            */
    logDecode("tab()");
    gotoXY((currentBuffer->cursorX + 8) & ~7, currentBuffer->cursorY);
    break;
  case 'j': /* Moves cursor up one row and scrolls if in top row             */
    logDecode("moveUpAndScroll()");
    gotoXYscroll(currentBuffer->cursorX, currentBuffer->cursorY-1);
    break;
  case 'k': /* Turns local edit submode on                                   */
    /* not supported: local edit mode */
    logDecode("enableLocalEditMode() /* NOT SUPPORTED */");
    break;
  case 'l': /* Turns duplex edit submode on                                  */
    /* not supported: local edit mode */
    logDecode("enableDuplexEditMode() /* NOT SUPPORTED */");
    break;
  case 'p': /* Sends all characters unformatted to auxiliary port            */
    /* not supported: auxiliary port */
    logDecode("sendAllCharactersToAux() /* NOT SUPPORTED */");
    break;
  case 'q': /* Turns the insert submode on                                   */
    logDecode("enableInsertMode()");
    if (enter_insert_mode && strcmp(enter_insert_mode, "@"))
      putCapability(enter_insert_mode);
    insertMode = 1;
    break;
  case 'r': /* Turns the insert submode off                                  */
    logDecode("disableInsertMode()");
    if (insertMode && exit_insert_mode && strcmp(exit_insert_mode, "@"))
      putCapability(exit_insert_mode);
    insertMode   = 0;
    break;
  case 's': /* Sends a message                                               */
    /* not supported: messages */
    logDecode("sendMessage() /* NOT SUPPORTED */");
    break;
  case 't': /* Erases from cursor position to the end of the row             */
    logDecode("clearToEndOfLine()");
    clearEol();
    break;
  case 'u': /* Turns the monitor submode off                                 */
    /* not supported: monitor mode */
    logDecode("disableMonitorMode() /* NOT SUPPORTED */");
    break;
  case 'w': /* Divide memory into pages; or select page to display           */
    mode         = E_SELECT_PAGE;
    break;
  case 'x': /* Changes the screen display format                             */
    /* not supported: text segments */
    mode         = E_SET_SEGMENT_POSITION;
    break;
  case 'y': /* Erases all characters from the cursor to end of text segment  */
    /* not supported: text segments */
    logDecode("clearToEndOfSegment() /* NOT SUPPORTED */");
    clearEos();
    break;
  case 'z': /* Enters message into key label field                           */
    logDecode("setKeyLabel() /* NOT SUPPORTED */ [");
    mode         = E_SKIP_DEL;
    break;
  case '{': /* Moves cursor to home position of text segment                 */
    /* not supported: text segments */
    logDecode("home()");
    gotoXY(0, 0);
    break;
  case '}': /* Activates text segment 1                                      */
    /* not supported: text segments */
    logDecode("activateSegment(0) /* NOT SUPPORTED */");
    break;
  case '~': /* Select personality                                            */
    /* not supported: personalities */
    logDecode("setPersonality() /* NOT SUPPORTED */ [");
    mode         = E_SKIP_ONE;
    break;
  }

  if (mode == E_NORMAL)
    logDecodeFlush();

  return;
}


static void normal(int pty, char ch) {
  switch (ch) {
  case '\x00': /* NUL: No action                                             */
    logDecode("nul() /* no action */");
    logDecodeFlush();
    break;
  case '\x01': /* SOH: No action                                             */
    logDecode("soh() /* no action */");
    logDecodeFlush();
    break;
  case '\x02': /* STX: No action                                             */
    if (mode == E_GRAPHICS_CHARACTER) {
      putGraphics(ch); /* doesn't actually output anything */
      mode                     = E_NORMAL;
    } else {
      logDecode("stx() /* no action */");
      logDecodeFlush();
    }
    break;
  case '\x03': /* ETX: No action                                             */
    if (mode == E_GRAPHICS_CHARACTER) {
      putGraphics(ch); /* doesn't actually output anything */
      mode                     = E_NORMAL;
    } else {
      logDecode("etx() /* no action */");
      logDecodeFlush();
    }
    break;
  case '\x04': /* EOT: No action                                             */
    logDecode("eot() /* no action */");
    logDecodeFlush();
    break;
  case '\x05': /* ENQ: Returns ACK, if not busy                              */
    logDecode("enq()");
    sendUserInput(pty, "\x06", 1);
    logDecodeFlush();
    break;
  case '\x06': /* ACK: No action                                             */
    logDecode("ack() /* no action */");
    logDecodeFlush();
    break;
  case '\x07': /* BEL: Sound beeper                                          */
    logDecode("bell()");
    if (bell && strcmp(bell, "@"))
      putCapability(bell);
    logDecodeFlush();
    break;
  case '\x08':{/* BS:  Move cursor to the left                               */
    int x                      = currentBuffer->cursorX - 1;
    int y                      = currentBuffer->cursorY;
    if (x < 0) {
      x                        = screenWidth - 1;
      if (--y < 0)
        y                      = 0;
    }
    logDecode("moveLeft()");
    gotoXY(x, y);
    logDecodeFlush();
    break; }
  case '\x09': /* HT:  Move to next tab position on the right                */
    logDecode("tab()");
    gotoXY((currentBuffer->cursorX + 8) & ~7, currentBuffer->cursorY);
    logDecodeFlush();
    break;
  case '\x0A': /* LF:  Moves cursor down                                     */
    logDecode("moveDown()");
    gotoXYscroll(currentBuffer->cursorX, currentBuffer->cursorY + 1);
    logDecodeFlush();
    break;
  case '\x0B': /* VT:  Moves cursor up                                       */
    logDecode("moveUp()");
    gotoXY(currentBuffer->cursorX,
           (currentBuffer->cursorY - 1 + screenHeight) % screenHeight);
    logDecodeFlush();
    break;
  case '\x0C': /* FF:  Moves cursor to the right                             */
    logDecode("moveRight()");
    gotoXY(currentBuffer->cursorX + 1, currentBuffer->cursorY);
    logDecodeFlush();
    break;
  case '\x0D': /* CR:  Moves cursor to column one                            */
    logDecode("return()");
    gotoXY(0, currentBuffer->cursorY);
    logDecodeFlush();
    break;
  case '\x0E': /* SO:  Unlocks the keyboard                                  */
    logDecode("so() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x0F': /* SI:  Locks the keyboard                                    */
    logDecode("si() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x10': /* DLE: No action                                             */
    logDecode("dle() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x11': /* XON: Enables the transmitter                               */
    logDecode("xon() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x12': /* DC2: Turns on auxiliary print                              */
    logDecode("dc2() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x13': /* XOFF:Stops transmission to host                            */
    logDecode("xoff() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x14': /* DC4: Turns off auxiliary print                             */
    logDecode("dc4() /* NOT SUPPORTED */");
    logDecodeFlush();
    break;
  case '\x15': /* NAK: No action                                             */
    logDecode("nak() /* no action */");
    logDecodeFlush();
    break;
  case '\x16': /* SYN: No action                                             */
    logDecode("syn() /* no action */");
    logDecodeFlush();
    break;
  case '\x17': /* ETB: No action                                             */
    logDecode("etb() /* no action */");
    logDecodeFlush();
    break;
  case '\x18': /* CAN: No action                                             */
    logDecode("can() /* no action */");
    logDecodeFlush();
    break;
  case '\x19': /* EM:  No action                                             */
    logDecode("em() /* no action */");
    logDecodeFlush();
    break;
  case '\x1A': /* SUB: Clears all unprotected characters                     */
    logDecode("clearScreen()");
    clearScreen();
    logDecodeFlush();
    break;
  case '\x1B': /* ESC: Initiates an escape sequence                          */
    mode                       = E_ESC;
    break;
  case '\x1C': /* FS:  No action                                             */
    logDecode("fs() /* no action */");
    logDecodeFlush();
    break;
  case '\x1D': /* GS:  No action                                             */
    logDecode("gs() /* no action */");
    logDecodeFlush();
    break;
  case '\x1E': /* RS:  Moves cursor to home position                         */
    logDecode("home()");
    gotoXY(0, 0);
    logDecodeFlush();
    break;
  case '\x1F': /* US:  Moves cursor down one row to column one               */
    logDecode("moveDown() ");
    logDecode("return()");
    gotoXYscroll(0, currentBuffer->cursorY + 1);
    logDecodeFlush();
    break;
  case '\x7F': /* DEL: Delete character                                      */
    logDecode("del() /* no action */");
    logDecodeFlush();
    break;
  default:
    /* Things get ugly when we get to the right margin, because terminals    */
    /* behave differently depending on whether they support auto margins and */
    /* on whether they have the eat-newline glitch (or a variation thereof)  */
    if (currentBuffer->cursorX == screenWidth-1 &&
        currentBuffer->cursorY == screenHeight-1) {
      /* If write protection has been enabled, then we do not want to auto-  */
      /* matically scroll the screen. This is rather difficult to implement  */
      /* because of the problems with writing to the very last character on  */
      /* the screen. We work around the problem, by going through all the    */
      /* steps for updating the the screen buffer and then forcing a full    */
      /* redraw. The redraw code knows how to write the last line without    */
      /* scrolling accidentally.                                             */
      if (writeProtection) {
        int cursorX            = currentBuffer->cursorX;
        int cursorY            = currentBuffer->cursorY;
        if (protected || insertMode ||
            (currentBuffer->attributes[cursorY][cursorX] & T_PROTECTED) == 0) {
          int attributes       = currentAttributes;
          if (protected)
            attributes        |= T_PROTECTED;
          if (currentAttributes & T_BLANK)
            ch                 = ' ';
          else if (graphicsMode || mode == E_GRAPHICS_CHARACTER) {
            if (ch >= '0' && ch <= '?')
              attributes      |= T_GRAPHICS;
            else
              ch               = ' ';
          }
          currentBuffer->attributes[cursorY][cursorX] = attributes;
          currentBuffer->lineBuffer[cursorY][cursorX] = ch;
          displayCurrentScreenBuffer();
        }
        break;
      } else {
        /* Play it save and scroll the screen before we do anything else     */
        gotoXYscroll(currentBuffer->cursorX, currentBuffer->cursorY + 1);
        gotoXY(currentBuffer->cursorX, currentBuffer->cursorY - 1);
      }
    }
    if (insertMode) {
      _moveScreenBuffer(currentBuffer,
	 	        currentBuffer->cursorX, currentBuffer->cursorY,
		        screenWidth - 1, currentBuffer->cursorY,
		        1, 0);
      if (!enter_insert_mode || !strcmp(enter_insert_mode, "@"))
        putCapability(insert_character);
    }

    /* If write-protection has been enabled then avoid overwriting write     */
    /* protected characters unless 1) we are outputting write protected char-*/
    /* characters, or 2) insert mode is enabled                              */
    if (!writeProtection || protected || insertMode ||
        (currentBuffer->attributes[currentBuffer->cursorY]
                                  [currentBuffer->cursorX] & T_PROTECTED)==0) {
      if (currentAttributes & T_BLANK)
        putConsole(' ');
      else if (graphicsMode || mode == E_GRAPHICS_CHARACTER) {
        putGraphics(ch);
        mode                   = E_NORMAL;
      } else
        putConsole(ch);
    } else if (currentBuffer->cursorX < screenWidth-1) {
      gotoXY(currentBuffer->cursorX + 1,
             currentBuffer->cursorY);
      currentBuffer->cursorX--;
    }
    if (++currentBuffer->cursorX >= screenWidth) {
      int x                    = 0;
      int y                    = currentBuffer->cursorY + 1;

      if (auto_right_margin && !eat_newline_glitch) {
        currentBuffer->cursorX = 0;
        currentBuffer->cursorY++;
      } else
        currentBuffer->cursorX = screenWidth-1;

      /* We want the cursor at the beginning of the next line, but at this   */
      /* time we are not absolutely sure, we know where the cursor currently */
      /* is. Force it to where we need it.                                   */
      gotoXYforce(x,y);
    }
    break;
  }
  return;
}


static void outputCharacter(int pty, char ch) {
  #ifdef DEBUG_LOG_NATIVE
  { static int logFd = -2;
  char         buffer[80];
  if (logFd == -2) {
    char       *logger;
    if ((logger          = getenv("WY60NATIVE")) != NULL) {
      logFd              = creat(logger, 0644);
    } else
      logFd              = -1;
  }
  if (logFd >= 0) {
    if (mode != E_NORMAL || (unsigned char)ch < (unsigned char)' ') {
      if (isatty(logFd))
        sprintf(buffer, "\x1B[33m");
      else
        buffer[0]        = '\000';
    } else
      buffer[0]          = '\000';
    if ((unsigned char)ch >= (unsigned char)' ')
      sprintf(strrchr(buffer, '\000'), "%c", ch);
    else
      sprintf(strrchr(buffer, '\000'), "^%c", ch | '@');
    if (mode != E_NORMAL || (unsigned char)ch < (unsigned char)' ') {
      if (isatty(logFd))
        sprintf(strchr(buffer, '\000'), "\x1B[39m");
    }
    flushConsole();
    write(logFd, buffer, strlen(buffer));
  } }
  #endif
  #ifdef DEBUG_SINGLE_STEP
  { static int logFd     = -2;
  if (logFd == -2) {
    char       *logger;
    if ((logger          = getenv("WY60SINGLESTEP")) != NULL) {
      logFd              = open(logger, O_RDONLY);
    } else
      logFd              = -1;
  }
  if (logFd >= 0) {
    char dummy;
    if (mode == E_NORMAL && (unsigned char)ch < (unsigned char)' ')
      read(logFd, &dummy, 1);
  } }
  #endif

  switch (mode) {
  case E_GRAPHICS_CHARACTER:
    logDecode("enterGraphicsCharacter(0x%02X)", ch);
    logDecodeFlush();
    /* fall thru */
  case E_NORMAL:
    normal(pty, ch);
    break;
  case E_ESC:
    escape(pty, ch);
    break;
  case E_SKIP_ONE:
    mode                 = E_NORMAL;
    logDecode(" 0x%02X ]", ch);
    logDecodeFlush();
    break;
  case E_SKIP_LINE:
    if (ch == '\r') {
      logDecode(" ]");
      mode               = E_NORMAL;
      logDecodeFlush();
    } else
      logDecode(" %02X", ch);
    break;
  case E_SKIP_DEL:
    if (ch == '\x7F' || ch == '\r') {
      logDecode(" ]");
      mode               = E_NORMAL;
      logDecodeFlush();
    } else
      logDecode(" %02X", ch);
    break;
  case E_FILL_SCREEN:
    logDecode("fillScreen(0x%02x)", ch);
    fillScreen(T_NORMAL, ch);
    break;
  case E_GOTO_SEGMENT:
    /* not supported: text segments */
    mode                 = E_GOTO_ROW_CODE;
    break;
  case E_GOTO_ROW_CODE:
    targetRow            = (((int)ch)&0xFF) - 32;
    mode                 = E_GOTO_COLUMN_CODE;
    break;
  case E_GOTO_COLUMN_CODE:
    logDecode("gotoXY(%d,%d)", (((int)ch)&0xFF) - 32, targetRow);
    gotoXY((((int)ch)&0xFF) - 32,targetRow);
    mode                 = E_NORMAL;
    logDecodeFlush();
    break;
  case E_GOTO_ROW:
    if (ch == 'R')
      mode               = E_GOTO_COLUMN;
    else
      targetRow          = 10*targetRow + (((int)(ch - '0'))&0xFF);
    break;
  case E_GOTO_COLUMN:
    if (ch == 'C') {
      logDecode("gotoXY(%d,%d)", targetColumn-1, targetRow-1);
      gotoXY(targetColumn-1, targetRow-1);
      mode               = E_NORMAL;
      logDecodeFlush();
    } else
      targetColumn       = 10*targetColumn + (((int)(ch - '0'))&0xFF);
    break;
  case E_SET_FIELD_ATTRIBUTE:
    if (ch != '0') {
      /* not supported: attributes for non-display areas */
      logDecode("NOT SUPPORTED [ 0x1B 0x41 0x02X", ch);
      mode               = E_SKIP_ONE;
    } else
      mode               = E_SET_ATTRIBUTE;
    break;
  case E_SET_ATTRIBUTE:
    logDecode("setAttribute(%s%s%s%s%s%s)",
              (ch & T_ALL) == T_NORMAL    ? " NORMAL"     : "",
              (ch & T_ALL) & T_REVERSE    ? " REVERSE"    : "",
              (ch & T_ALL) & T_DIM        ? " DIM"        : "",
              (ch & T_ALL) & T_UNDERSCORE ? " UNDERSCORE" : "",
              (ch & T_ALL) & T_BLINK      ? " BLINK"      : "",
              (ch & T_ALL) & T_BLANK      ? " BLANK"      : "");
    setAttributes(ch);
    mode                 = E_NORMAL;
    logDecodeFlush();
    break;
  case E_SET_FEATURES:
    switch (ch) {
    case '0': /* Cursor display off                                          */
      showCursor(0);
      logDecode("hideCursor()");
      break;
    case '1': /* Cursor display on                                           */
    case '2': /* Steady block cursor                                         */
    case '5': /* Blinking block cursor                                       */
      showCursor(1);
      logDecode("showCursor()");
      break;
    case '3': /* Blinking line cursor                                        */
    case '4': /* Steady line cursor                                          */
      showCursor(1);
      logDecode("dimCursor()");
      break;
    case '6': /* Reverse protected character                                 */
      setFeatures(T_REVERSE);
      logDecode("reverseProtectedCharacters()");
      break;
    case '7': /* Dim protected character                                     */
      setFeatures(T_DIM);
      logDecode("dimProtectedCharacters()");
      break;
    case '8': /* Screen display off                                          */
    case '9': /* Screen display on                                           */
      /* not supported: disabling screen display */
      logDecode("NOT SUPPORTED [ 0x1B 0x60 0x%02X ]", ch);
      break;
    case ':':{/* 80 column mode                                              */
      int newWidth;
      newWidth           = 80;
      goto setWidth;
    case ';': /* 132 column mode                                             */
      newWidth           = 132;
    setWidth:
      requestNewGeometry(pty, newWidth, nominalHeight);
      break; }
    case '<': /* Smooth scroll at one row per second                         */
    case '=': /* Smooth scroll at two rows per second                        */
    case '>': /* Smooth scroll at four rows per second                       */
    case '?': /* Smooth scroll at eight rows per second                      */
    case '@': /* Jump scroll                                                 */
      /* not supported: selecting scroll speed */
      logDecode("NOT SUPPORTED [ 0x1B 0x60 0x%02X ]", ch);
      break;
    case 'A': /* Normal protected character                                  */
      setFeatures(T_NORMAL);
      logDecode("normalProtectedCharacters()");
      break;
    }
    mode                 = E_NORMAL;
    logDecodeFlush();
    break;
  case E_FUNCTION_KEY:
    logDecode("NOT SUPPORTED [ 0x1B 0x5A 0x%02X", ch);
    if (ch == '~') {
      /* not supported: programming function keys */
      mode               = E_SKIP_ONE;
    } else {
      /* not supported: programming function keys */
      mode               = E_SKIP_DEL;
    }
    break;
  case E_SET_SEGMENT_POSITION:
    logDecode("NOT SUPPORTED [ 0x1B 0x78 0x%02X", ch);
    if (ch == '0') {
      /* not supported: text segments */
      logDecode(" ]");
      mode               = E_NORMAL;
      logDecodeFlush();
    } else {
      /* not supported: text segments */
      mode               = E_SKIP_ONE;
    }
    break;
  case E_SELECT_PAGE:
    switch (ch) {
    case 'G': /* Page size equals number of data lines                       */
    case 'H': /* Page size is twice the number of data lines                 */
    case 'J': /* 1st page is number of data lines,2nd page is remaining lines*/
      /* not supported: splitting memory */
      logDecode("NOT SUPPORTED [ 0x1B 0x77 0x%02X ]", ch);
      break;
    case 'B': /* Display previous page                                       */
      logDecode("displayPreviousPage()");
      setPage(currentPage - 1);
      break;
    case 'C': /* Display next page                                           */
      logDecode("displayNextPage()");
      setPage(currentPage + 1);
      break;
    case '0': /* Display page 0                                              */
      logDecode("displayPage(0)");
      setPage(0);
      break;
    case '1': /* Display page 1                                              */
      logDecode("displayPage(1)");
      setPage(1);
      break;
    case '2': /* Display page 2                                              */
      /* not supported: page 2 */
      logDecode("NOT SUPPORTED [ 0x1B 0x77 0x32 ]");
      setPage(2);
      break;
    }
    mode                 = E_NORMAL;
    logDecodeFlush();
    break;
  case E_CSI_E:
    switch (ch) {
      int newHeight;
    case '(': /* Display 24 data lines                                       */
      newHeight          = 24;
      goto setHeight;
    case ')': /* Display 25 data lines                                       */
      newHeight          = 25;
      goto setHeight;
    case '*': /* Display 42 data lines                                       */
      newHeight          = 42;
      goto setHeight;
    case '+': /* Display 43 data lines                                       */
      newHeight          = 43;
    setHeight:
      requestNewGeometry(pty, nominalWidth, newHeight);
      break;
    default:
      logDecode("setCommunicationMode(0x%02X) /* NOT SUPPORTED */", ch);
      break;
    }
    mode                 = E_NORMAL;
    logDecodeFlush();
    break;
  }
  return;
}


static void initKeyboardTranslations(void) {
  addKeyboardTranslation("A1",               key_a1,       cfgA1);
  addKeyboardTranslation("A3",               key_a3,       cfgA3);
  addKeyboardTranslation("B2",               key_b2,       cfgB2);
  addKeyboardTranslation("C1",               key_c1,       cfgC1);
  addKeyboardTranslation("C3",               key_c3,       cfgC3);

  addKeyboardTranslation("Backspace",        key_backspace,cfgBackspace);
  addKeyboardTranslation("Backtab",          key_btab,     cfgBacktab);
  addKeyboardTranslation("Begin",            key_beg,      cfgBegin);
  addKeyboardTranslation("Cancel",           key_cancel,   cfgCancel);
  addKeyboardTranslation("Clear All Tabs",   key_catab,    cfgClear);
  addKeyboardTranslation("Clear Tab",        key_ctab,     cfgClearAllTabs);
  addKeyboardTranslation("Clear",            key_clear,    cfgClearTab);
  addKeyboardTranslation("Close",            key_close,    cfgClose);
  addKeyboardTranslation("Command",          key_command,  cfgCommand);
  addKeyboardTranslation("Copy",             key_copy,     cfgCopy);
  addKeyboardTranslation("Create",           key_create,   cfgCreate);
  addKeyboardTranslation("Delete Line",      key_dl,       cfgDelete);
  addKeyboardTranslation("Delete",           key_dc,       cfgDeleteLine);
  addKeyboardTranslation("Down",             key_down,     cfgDown);
  addKeyboardTranslation("End Of Line",      key_eol,      cfgEnd);
  addKeyboardTranslation("End Of Screen",    key_eos,      cfgEndOfLine);
  addKeyboardTranslation("End",              key_end,      cfgEndOfScreen);
  addKeyboardTranslation("Enter",            key_enter,    cfgEnter);
  addKeyboardTranslation("Exit Insert Mode", key_eic,      cfgExit);
  addKeyboardTranslation("Exit",             key_exit,     cfgExitInsertMode);
  addKeyboardTranslation("Find",             key_find,     cfgFind);
  addKeyboardTranslation("Help",             key_help,     cfgHelp);
  addKeyboardTranslation("Home",             key_home,     cfgHome);
  addKeyboardTranslation("Insert Line",      key_il,       cfgInsert);
  addKeyboardTranslation("Insert",           key_ic,       cfgInsertLine);
  addKeyboardTranslation("Left",             key_left,     cfgLeft);
  addKeyboardTranslation("Lower Left",       key_ll,       cfgLowerLeft);
  addKeyboardTranslation("Mark",             key_mark,     cfgMark);
  addKeyboardTranslation("Message",          key_message,  cfgMessage);
  addKeyboardTranslation("Move",             key_move,     cfgMove);
  addKeyboardTranslation("Next",             key_next,     cfgNext);
  addKeyboardTranslation("Open",             key_open,     cfgOpen);
  addKeyboardTranslation("Options",          key_options,  cfgOptions);
  addKeyboardTranslation("Page Down",        key_npage,    cfgPageDown);
  addKeyboardTranslation("Page Up",          key_ppage,    cfgPageUp);
  addKeyboardTranslation("Previous",         key_previous, cfgPrevious);
  addKeyboardTranslation("Print",            key_print,    cfgPrint);
  addKeyboardTranslation("Redo",             key_redo,     cfgRedo);
  addKeyboardTranslation("Reference",        key_reference,cfgReference);
  addKeyboardTranslation("Refresh",          key_refresh,  cfgRefresh);
  addKeyboardTranslation("Replace",          key_replace,  cfgReplace);
  addKeyboardTranslation("Restart",          key_restart,  cfgRestart);
  addKeyboardTranslation("Resume",           key_resume,   cfgResume);
  addKeyboardTranslation("Right",            key_right,    cfgRight);
  addKeyboardTranslation("Save",             key_save,     cfgSave);
  addKeyboardTranslation("Scroll Down",      key_sf,       cfgScrollDown);
  addKeyboardTranslation("Scroll Up",        key_sr,       cfgScrollUp);
  addKeyboardTranslation("Select",           key_select,   cfgSelect);
  addKeyboardTranslation("Set Tab",          key_stab,     cfgSetTab);
  addKeyboardTranslation("Suspend",          key_suspend,  cfgSuspend);
  addKeyboardTranslation("Undo",             key_undo,     cfgUndo);
  addKeyboardTranslation("Up",               key_up,       cfgUp);

  addKeyboardTranslation("Shift Begin",      key_sbeg,     cfgShiftBegin);
  addKeyboardTranslation("Shift Cancel",     key_scancel,  cfgShiftCancel);
  addKeyboardTranslation("Shift Command",    key_scommand, cfgShiftCommand);
  addKeyboardTranslation("Shift Copy",       key_scopy,    cfgShiftCopy);
  addKeyboardTranslation("Shift Create",     key_screate,  cfgShiftCreate);
  addKeyboardTranslation("Shift Delete Line",key_sdl,      cfgShiftDelete);
  addKeyboardTranslation("Shift Delete",     key_sdc,      cfgShiftDeleteLine);
  addKeyboardTranslation("Shift End Of Line",key_seol,     cfgShiftEnd);
  addKeyboardTranslation("Shift End",        key_send,     cfgShiftEndOfLine);
  addKeyboardTranslation("Shift Exit",       key_sexit,    cfgShiftExit);
  addKeyboardTranslation("Shift Find",       key_sfind,    cfgShiftFind);
  addKeyboardTranslation("Shift Help",       key_shelp,    cfgShiftHelp);
  addKeyboardTranslation("Shift Home",       key_shome,    cfgShiftHome);
  addKeyboardTranslation("Shift Insert",     key_sic,      cfgShiftInsert);
  addKeyboardTranslation("Shift Left",       key_sleft,    cfgShiftLeft);
  addKeyboardTranslation("Shift Message",    key_smessage, cfgShiftMessage);
  addKeyboardTranslation("Shift Move",       key_smove,    cfgShiftMove);
  addKeyboardTranslation("Shift Next",       key_snext,    cfgShiftNext);
  addKeyboardTranslation("Shift Options",    key_soptions, cfgShiftOptions);
  addKeyboardTranslation("Shift Previous",   key_sprevious,cfgShiftPrevious);
  addKeyboardTranslation("Shift Print",      key_sprint,   cfgShiftPrint);
  addKeyboardTranslation("Shift Redo",       key_sredo,    cfgShiftRedo);
  addKeyboardTranslation("Shift Replace",    key_sreplace, cfgShiftReplace);
  addKeyboardTranslation("Shift Resume",     key_srsume,   cfgShiftResume);
  addKeyboardTranslation("Shift Right",      key_sright,   cfgShiftRight);
  addKeyboardTranslation("Shift Save",       key_ssave,    cfgShiftSave);
  addKeyboardTranslation("Shift Suspend",    key_ssuspend, cfgShiftSuspend);
  addKeyboardTranslation("Shift Undo",       key_sundo,    cfgShiftUndo);

  /* These defaults work fine with xterm using a standard PC keyboard and    */
  /* with the terminfo entry that ships with xterm. If these settings don't  */
  /* match with what the user expects, they can be overridden in the         */
  /* wy60.rc configuration file.                                             */
  addKeyboardTranslation("F0",               key_f0,       cfgF0);
  addKeyboardTranslation("F1",               key_f1,       cfgF1);
  addKeyboardTranslation("F2",               key_f2,       cfgF2);
  addKeyboardTranslation("F3",               key_f3,       cfgF3);
  addKeyboardTranslation("F4",               key_f4,       cfgF4);
  addKeyboardTranslation("F5",               key_f5,       cfgF5);
  addKeyboardTranslation("F6",               key_f6,       cfgF6);
  addKeyboardTranslation("F7",               key_f7,       cfgF7);
  addKeyboardTranslation("F8",               key_f8,       cfgF8);
  addKeyboardTranslation("F9",               key_f9,       cfgF9);
  addKeyboardTranslation("F10",              key_f10,      cfgF10);
  addKeyboardTranslation("F11",              key_f11,      cfgF11);
  addKeyboardTranslation("F12",              key_f12,      cfgF12);
  addKeyboardTranslation("F13",              key_f13,      cfgF13);
  addKeyboardTranslation("F14",              key_f14,      cfgF14);
  addKeyboardTranslation("F15",              key_f15,      cfgF15);
  addKeyboardTranslation("F16",              key_f16,      cfgF16);
  addKeyboardTranslation("F17",              key_f17,      cfgF17);
  addKeyboardTranslation("F18",              key_f18,      cfgF18);
  addKeyboardTranslation("F19",              key_f19,      cfgF19);
  addKeyboardTranslation("F20",              key_f20,      cfgF20);
  addKeyboardTranslation("F21",              key_f21,      cfgF21);
  addKeyboardTranslation("F22",              key_f22,      cfgF22);
  addKeyboardTranslation("F23",              key_f23,      cfgF23);
  addKeyboardTranslation("F24",              key_f24,      cfgF24);
  addKeyboardTranslation("F25",              key_f25,      cfgF25);
  addKeyboardTranslation("F26",              key_f26,      cfgF26);
  addKeyboardTranslation("F27",              key_f27,      cfgF27);
  addKeyboardTranslation("F28",              key_f28,      cfgF28);
  addKeyboardTranslation("F29",              key_f29,      cfgF29);
  addKeyboardTranslation("F30",              key_f30,      cfgF30);
  addKeyboardTranslation("F31",              key_f31,      cfgF31);
  addKeyboardTranslation("F32",              key_f32,      cfgF32);
  addKeyboardTranslation("F33",              key_f33,      cfgF33);
  addKeyboardTranslation("F34",              key_f34,      cfgF34);
  addKeyboardTranslation("F35",              key_f35,      cfgF35);
  addKeyboardTranslation("F36",              key_f36,      cfgF36);
  addKeyboardTranslation("F37",              key_f37,      cfgF37);
  addKeyboardTranslation("F38",              key_f38,      cfgF38);
  addKeyboardTranslation("F39",              key_f39,      cfgF39);
  addKeyboardTranslation("F40",              key_f40,      cfgF40);
  addKeyboardTranslation("F41",              key_f41,      cfgF41);
  addKeyboardTranslation("F42",              key_f42,      cfgF42);
  addKeyboardTranslation("F43",              key_f43,      cfgF43);
  addKeyboardTranslation("F44",              key_f44,      cfgF44);
  addKeyboardTranslation("F45",              key_f45,      cfgF45);
  addKeyboardTranslation("F46",              key_f46,      cfgF46);
  addKeyboardTranslation("F47",              key_f47,      cfgF47);
  addKeyboardTranslation("F48",              key_f48,      cfgF48);
  addKeyboardTranslation("F49",              key_f49,      cfgF49);
  addKeyboardTranslation("F50",              key_f50,      cfgF50);
  addKeyboardTranslation("F51",              key_f51,      cfgF51);
  addKeyboardTranslation("F52",              key_f52,      cfgF52);
  addKeyboardTranslation("F53",              key_f53,      cfgF53);
  addKeyboardTranslation("F54",              key_f54,      cfgF54);
  addKeyboardTranslation("F55",              key_f55,      cfgF55);
  addKeyboardTranslation("F56",              key_f56,      cfgF56);
  addKeyboardTranslation("F57",              key_f57,      cfgF57);
  addKeyboardTranslation("F58",              key_f58,      cfgF58);
  addKeyboardTranslation("F59",              key_f59,      cfgF59);
  addKeyboardTranslation("F60",              key_f60,      cfgF60);
  addKeyboardTranslation("F61",              key_f61,      cfgF61);
  addKeyboardTranslation("F62",              key_f62,      cfgF62);
  addKeyboardTranslation("F63",              key_f63,      cfgF63);

  /* Add a couple of commonly used key definitions as fallbacks in case the  */
  /* terminfo entry is incomplete or incorrect (this happens pretty          */
  /* frequently).                                                            */
  addKeyboardTranslation("Backspace",        "\x7F",       cfgBackspace);
  addKeyboardTranslation("Backtab",          "\x1B[Z",     cfgBacktab);
  addKeyboardTranslation("Backtab",          "\x1B[5Z",    cfgBacktab);
  addKeyboardTranslation("Delete",           "\x1B[3~",    cfgDelete);
  addKeyboardTranslation("Delete",           "\x1B[3;5~",  cfgDelete);
  addKeyboardTranslation("Down",             "\x1B[B",     cfgDown);
  addKeyboardTranslation("Down",             "\x1B[2B",    cfgDown);
  addKeyboardTranslation("Down",             "\x1B[5B",    cfgDown);
  addKeyboardTranslation("Down",             "\x1BOB",     cfgDown);
  addKeyboardTranslation("Down",             "\x1BO2B",    cfgDown);
  addKeyboardTranslation("Down",             "\x1BO5B",    cfgDown);
  addKeyboardTranslation("End",              "\x1B[4~",    cfgEnd);
  addKeyboardTranslation("End",              "\x1B[4;5~",  cfgEnd);
  addKeyboardTranslation("End",              "\x1B[8~",    cfgEnd);
  addKeyboardTranslation("End",              "\x1B[8;5~",  cfgEnd);
  addKeyboardTranslation("End",              "\x1B[F",     cfgEnd);
  addKeyboardTranslation("End",              "\x1B[5F",    cfgEnd);
  addKeyboardTranslation("End",              "\x1BOF",     cfgEnd);
  addKeyboardTranslation("End",              "\x1BO5F",    cfgEnd);
  addKeyboardTranslation("Enter",            "\x1B[M",     cfgEnter);
  addKeyboardTranslation("Enter",            "\x1B[2M",    cfgEnter);
  addKeyboardTranslation("Enter",            "\x1B[5M",    cfgEnter);
  addKeyboardTranslation("Enter",            "\x1BOM",     cfgEnter);
  addKeyboardTranslation("Enter",            "\x1BO2M",    cfgEnter);
  addKeyboardTranslation("Enter",            "\x1BO5M",    cfgEnter);
  addKeyboardTranslation("Home",             "\x1B[1~",    cfgHome);
  addKeyboardTranslation("Home",             "\x1B[1;5~",  cfgHome);
  addKeyboardTranslation("Home",             "\x1B[H",     cfgHome);
  addKeyboardTranslation("Home",             "\x1B[5H",    cfgHome);
  addKeyboardTranslation("Home",             "\x1BOH",     cfgHome);
  addKeyboardTranslation("Home",             "\x1BO5H",    cfgHome);
  addKeyboardTranslation("Insert",           "\x1B[2~",    cfgInsert);
  addKeyboardTranslation("Insert",           "\x1B[2;5~",  cfgInsert);
  addKeyboardTranslation("Left",             "\x1B[D",     cfgLeft);
  addKeyboardTranslation("Left",             "\x1B[5D",    cfgLeft);
  addKeyboardTranslation("Left",             "\x1BOD",     cfgLeft);
  addKeyboardTranslation("Left",             "\x1BO5D",    cfgLeft);
  addKeyboardTranslation("Page Down",        "\x1B[6~",    cfgPageDown);
  addKeyboardTranslation("Page Down",        "\x1B[6;5~",  cfgPageDown);
  addKeyboardTranslation("Page Up",          "\x1B[5~",    cfgPageUp);
  addKeyboardTranslation("Page Up",          "\x1B[5;5~",  cfgPageUp);
  addKeyboardTranslation("Right",            "\x1B[C",     cfgRight);
  addKeyboardTranslation("Right",            "\x1B[5C",    cfgRight);
  addKeyboardTranslation("Right",            "\x1BOC",     cfgRight);
  addKeyboardTranslation("Right",            "\x1BO5C",    cfgRight);
  addKeyboardTranslation("Up",               "\x1B[A",     cfgUp);
  addKeyboardTranslation("Up",               "\x1B[2A",    cfgUp);
  addKeyboardTranslation("Up",               "\x1B[5A",    cfgUp);
  addKeyboardTranslation("Up",               "\x1BOA",     cfgUp);
  addKeyboardTranslation("Up",               "\x1BO2A",    cfgUp);
  addKeyboardTranslation("Up",               "\x1BO5A",    cfgUp);

  addKeyboardTranslation("Shift Delete",     "\x1B[3;2~",  cfgShiftDelete);
  addKeyboardTranslation("Shift End",        "\x1B[4;2~",  cfgShiftEnd);
  addKeyboardTranslation("Shift End",        "\x1B[8;2~",  cfgShiftEnd);
  addKeyboardTranslation("Shift End",        "\x1B[2F",    cfgShiftEnd);
  addKeyboardTranslation("Shift End",        "\x1BO2F",    cfgShiftEnd);
  addKeyboardTranslation("Shift Home",       "\x1B[1;2~",  cfgShiftHome);
  addKeyboardTranslation("Shift Home",       "\x1B[2H",    cfgShiftHome);
  addKeyboardTranslation("Shift Home",       "\x1BO2H",    cfgShiftHome);
  addKeyboardTranslation("Shift Insert",     "\x1B[2;2~",  cfgShiftInsert);
  addKeyboardTranslation("Shift Left",       "\x1B[2D",    cfgShiftLeft);
  addKeyboardTranslation("Shift Left",       "\x1BO2D",    cfgShiftLeft);
  addKeyboardTranslation("Shift Next",       "\x1B[6;2~",  cfgShiftNext);
  addKeyboardTranslation("Shift Previous",   "\x1B[5;2~",  cfgShiftPrevious);
  addKeyboardTranslation("Shift Right",      "\x1B[2C",    cfgShiftRight);
  addKeyboardTranslation("Shift Right",      "\x1BO2C",    cfgShiftRight);

  addKeyboardTranslation("F1",               "\x1B[M",     cfgF1);
  addKeyboardTranslation("F1",               "\x1BOP",     cfgF1);
  addKeyboardTranslation("F1",               "\x1B[11~",   cfgF1);
  addKeyboardTranslation("F1",               "\x1B[[A",    cfgF1);
  addKeyboardTranslation("F2",               "\x1B[N",     cfgF2);
  addKeyboardTranslation("F2",               "\x1BOQ",     cfgF2);
  addKeyboardTranslation("F2",               "\x1B[12~",   cfgF2);
  addKeyboardTranslation("F2",               "\x1B[[B",    cfgF2);
  addKeyboardTranslation("F3",               "\x1B[O",     cfgF3);
  addKeyboardTranslation("F3",               "\x1BOR",     cfgF3);
  addKeyboardTranslation("F3",               "\x1B[13~",   cfgF3);
  addKeyboardTranslation("F3",               "\x1B[[C",    cfgF3);
  addKeyboardTranslation("F4",               "\x1B[P",     cfgF4);
  addKeyboardTranslation("F4",               "\x1BOS",     cfgF4);
  addKeyboardTranslation("F4",               "\x1B[14~",   cfgF4);
  addKeyboardTranslation("F4",               "\x1B[[D",    cfgF4);
  addKeyboardTranslation("F5",               "\x1B[Q",     cfgF5);
  addKeyboardTranslation("F5",               "\x1B[15~",   cfgF5);
  addKeyboardTranslation("F5",               "\x1B[[E",    cfgF5);
  addKeyboardTranslation("F6",               "\x1B[R",     cfgF6);
  addKeyboardTranslation("F6",               "\x1B[17~",   cfgF6);
  addKeyboardTranslation("F7",               "\x1B[S",     cfgF7);
  addKeyboardTranslation("F7",               "\x1B[18~",   cfgF7);
  addKeyboardTranslation("F8",               "\x1B[T",     cfgF8);
  addKeyboardTranslation("F8",               "\x1B[19~",   cfgF8);
  addKeyboardTranslation("F9",               "\x1B[U",     cfgF9);
  addKeyboardTranslation("F9",               "\x1B[20~",   cfgF9);
  addKeyboardTranslation("F10",              "\x1B[V",     cfgF10);
  addKeyboardTranslation("F10",              "\x1B[21~",   cfgF10);
  addKeyboardTranslation("F11",              "\x1B[W",     cfgF11);
  addKeyboardTranslation("F11",              "\x1B[23~",   cfgF11);
  addKeyboardTranslation("F12",              "\x1B[X",     cfgF12);
  addKeyboardTranslation("F12",              "\x1B[24~",   cfgF12);
  addKeyboardTranslation("F13",              "\x1B[Y",     cfgF13);
  addKeyboardTranslation("F13",              "\x1BO2P",    cfgF13);
  addKeyboardTranslation("F13",              "\x1BO5P",    cfgF13);
  addKeyboardTranslation("F13",              "\x1B[25~",   cfgF13);
  addKeyboardTranslation("F13",              "\x1B[[2A",   cfgF13);
  addKeyboardTranslation("F13",              "\x1B[[5A",   cfgF13);
  addKeyboardTranslation("F14",              "\x1BO2Q",    cfgF14);
  addKeyboardTranslation("F14",              "\x1BO5Q",    cfgF14);
  addKeyboardTranslation("F14",              "\x1B[26~",   cfgF14);
  addKeyboardTranslation("F14",              "\x1B[[2B",   cfgF14);
  addKeyboardTranslation("F14",              "\x1B[[5B",   cfgF14);
  addKeyboardTranslation("F15",              "\x1BO2R",    cfgF15);
  addKeyboardTranslation("F15",              "\x1BO5R",    cfgF15);
  addKeyboardTranslation("F15",              "\x1B[27~",   cfgF15);
  addKeyboardTranslation("F15",              "\x1B[[2C",   cfgF15);
  addKeyboardTranslation("F15",              "\x1B[[5C",   cfgF15);
  addKeyboardTranslation("F16",              "\x1BO2S",    cfgF16);
  addKeyboardTranslation("F16",              "\x1BO5S",    cfgF16);
  addKeyboardTranslation("F16",              "\x1B[28~",   cfgF16);
  addKeyboardTranslation("F16",              "\x1B[[2D",   cfgF16);
  addKeyboardTranslation("F16",              "\x1B[[5D",   cfgF16);
  addKeyboardTranslation("F17",              "\x1B[15;2~", cfgF17);
  addKeyboardTranslation("F17",              "\x1B[15;5~", cfgF17);
  addKeyboardTranslation("F17",              "\x1B[29~",   cfgF17);
  addKeyboardTranslation("F17",              "\x1B[[2E",   cfgF17);
  addKeyboardTranslation("F17",              "\x1B[[5E",   cfgF17);
  addKeyboardTranslation("F18",              "\x1B[17;2~", cfgF18);
  addKeyboardTranslation("F18",              "\x1B[17;5~", cfgF18);
  addKeyboardTranslation("F18",              "\x1B[30~",   cfgF18);
  addKeyboardTranslation("F19",              "\x1B[18;2~", cfgF19);
  addKeyboardTranslation("F19",              "\x1B[18;5~", cfgF19);
  addKeyboardTranslation("F19",              "\x1B[31~",   cfgF19);
  addKeyboardTranslation("F20",              "\x1B[19;2~", cfgF20);
  addKeyboardTranslation("F20",              "\x1B[19;5~", cfgF20);
  addKeyboardTranslation("F20",              "\x1B[32~",   cfgF20);
  addKeyboardTranslation("F21",              "\x1B[20;2~", cfgF21);
  addKeyboardTranslation("F21",              "\x1B[20;5~", cfgF21);
  addKeyboardTranslation("F21",              "\x1B[33~",   cfgF21);
  addKeyboardTranslation("F22",              "\x1B[21;2~", cfgF22);
  addKeyboardTranslation("F22",              "\x1B[21;5~", cfgF22);
  addKeyboardTranslation("F22",              "\x1B[34~",   cfgF22);
  addKeyboardTranslation("F23",              "\x1B[23;2~", cfgF23);
  addKeyboardTranslation("F23",              "\x1B[23;5~", cfgF23);
  addKeyboardTranslation("F23",              "\x1B[35~",   cfgF23);
  addKeyboardTranslation("F24",              "\x1B[24;2~", cfgF24);
  addKeyboardTranslation("F24",              "\x1B[24;5~", cfgF24);
  addKeyboardTranslation("F24",              "\x1B[36~",   cfgF24);

  return;
}


static void checkCapabilities(void) {
  if (!delete_character      || !strcmp(delete_character,  "@")   ||
      !delete_line           || !strcmp(delete_line,       "@")   ||
      !((insert_line         &&  strcmp(insert_line,       "@"))  ||
        (parm_insert_line    &&  strcmp(parm_insert_line,  "@"))) ||
      !((enter_insert_mode   &&  strcmp(enter_insert_mode, "@"))  ||
        (insert_character    &&  strcmp(insert_character,  "@"))) ||
      !((cursor_address      &&  strcmp(cursor_address,    "@"))  ||
        (((cursor_up         &&  strcmp(cursor_up,         "@"))  ||
          (parm_up_cursor    &&  strcmp(parm_up_cursor,    "@"))) &&
         ((cursor_down       &&  strcmp(cursor_down,       "@"))  ||
          (parm_down_cursor  &&  strcmp(parm_down_cursor,  "@"))) &&
         ((cursor_left       &&  strcmp(cursor_left,       "@"))  ||
          (parm_left_cursor  &&  strcmp(parm_left_cursor,  "@"))) &&
         ((cursor_right      &&  strcmp(cursor_right,      "@"))  ||
          (parm_right_cursor &&  strcmp(parm_right_cursor, "@"))))))
    failure(127, "Terminal has insufficient capabilities");
  return;
}


static void initTerminal(int pty) {
  static int       isRunning   = 0;
  char             buffer[80];
  struct termios   termios;
  struct winsize   win;
  int              i;

  /* Determine initial screen size                                           */
  if (ioctl(1, TIOCGWINSZ, &win) < 0 ||
      win.ws_col <= 0 || win.ws_row <= 0) {
    failure(127, "Cannot determine terminal size");
  }
  if (!isRunning) {
    originalWidth              =
    screenWidth                = win.ws_col;
    originalHeight             =
    screenHeight               = win.ws_row;

    /* Come up with a reasonable approximation as to which mode (80 or 132   */
    /* columns; and 24, 25, 42, or 43 lines) we are in at startup. We need   */
    /* this information when the application requests a mode change, because */
    /* it always modifies just one dimension at a time.                      */
    if (screenWidth <= (80+132)/2)
      nominalWidth             = 80;
    else
      nominalWidth             = 132;
    if (screenHeight <= 24)
      nominalHeight            = 24;
    else if (screenHeight <= (25+42)/2)
      nominalHeight            = 25;
    else if (screenHeight <= 42)
      nominalHeight            = 42;
    else
      nominalHeight            = 43;

    tcgetattr(1, &defaultTermios);
  }

  needsReset                   = 1;
  setupterm(NULL, 1, NULL);

  checkCapabilities();
  sendResetStrings();

  /* Set up the terminal for raw mode communication                          */
  memcpy(&termios, &defaultTermios, sizeof(termios));
#if HAVE_CFMAKERAW
  cfmakeraw(&termios);
#else
  termios.c_iflag             &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|
                                   ICRNL|IXON|IXOFF|IMAXBEL);
  termios.c_oflag             &= ~(OPOST|ONLCR|OCRNL|ONOCR|ONLRET|OFILL|OFDEL);
  termios.c_lflag             &= ~(ECHO|ECHOE|ECHOK|ECHONL|ICANON|ISIG|IEXTEN);
  termios.c_cflag             &= ~(CSIZE|PARENB);
  termios.c_cflag             |= CS8;
  termios.c_cc[VMIN]           = 1;
  termios.c_cc[VTIME]          = 0;
#endif
  tcsetattr(0, TCSANOW, &termios);
 
  if (!isRunning) {
    /* Enable all of our screen buffers                                      */
    for (i = 0; i < sizeof(screenBuffer)/sizeof(ScreenBuffer *); i++)
      screenBuffer[i]          = adjustScreenBuffer(NULL,
                                                    screenWidth, screenHeight);
    currentBuffer              = screenBuffer[currentPage];
  } else {
    int oldWidth               = screenWidth;
    int oldHeight              = screenHeight;

    /* Enable all of our screen buffers                                      */
    for (i = 0; i < sizeof(screenBuffer)/sizeof(ScreenBuffer *); i++)
      screenBuffer[i]          = adjustScreenBuffer(screenBuffer[i],
                                                    screenWidth, screenHeight);
    currentBuffer              = screenBuffer[currentPage];
    screenWidth                = win.ws_col;
    screenHeight               = win.ws_row;
    requestNewGeometry(pty, oldWidth, oldHeight);
  }
  
  /* Enable alternate character set (if neccessary)                          */
  if (ena_acs && strcmp(ena_acs, "@"))
    putCapability(ena_acs);

  /* With some terminals, we can determine the current cursor position; this */
  /* allows for seamlessly switching the emulation. With other terminals,    */
  /* this is not possible and we must clear the screen at startup.           */
  /* Unfortunately, the terminfo database does not have any support for this */
  /* capability, so we just have to resort to some reasonable heuristics.    */
  if (!strcmp(cursor_address, "\x1B[%i%p1%d;%p2%dH")) {
    /* This looks like a VT style terminal                                   */
    readResponse(500, "\x1B[0c", buffer, '\x1B', 'c', '\000', sizeof(buffer));
    if (*buffer != '\000') {
      vtStyleCursorReporting   = 1;
      if (!queryCursorPosition(&currentBuffer->cursorX,
			       &currentBuffer->cursorY)) {
        vtStyleCursorReporting = 0;
      }
    }
  } else if (!strcmp(cursor_address, "\x1B=%p1%\' \'%+%c%p2%\' \'%+%c")) {
    /* This looks like a wy60 style terminal                                 */
    wyStyleCursorReporting     = 1;
    if (!queryCursorPosition(&currentBuffer->cursorX,
                             &currentBuffer->cursorY))
      wyStyleCursorReporting   = 0;
  }

  /* Cursor reporting is not available; clear the screen so that we are in a */
  /* well defined state.                                                     */
  if (!vtStyleCursorReporting && !wyStyleCursorReporting) {
    currentBuffer->cursorX     =
    currentBuffer->cursorY     = 0;
    if (clear_screen)
      clearScreen();
    else
      gotoXYforce(0,0);
  }

  isRunning                    = 1;

  return;
}


static void processSignal(int signalNumber, int pid, int pty) {
  switch (signalNumber) {
  case SIGHUP:
  case SIGINT:
  case SIGQUIT:
  case SIGILL:
  case SIGTRAP:
  case SIGABRT:
  case SIGBUS:
  case SIGFPE:
  case SIGUSR1:
  case SIGSEGV:
  case SIGUSR2:
  case SIGPIPE:
  case SIGTERM:
  case SIGXCPU:
  case SIGXFSZ:
  case SIGVTALRM:
  case SIGPROF:
  case SIGIO:
    failure(126, "Exiting on signal %d", signalNumber);
  case SIGALRM:
    break;
  case SIGTSTP: {
    sigset_t         mask;
    struct sigaction action, old;

    if (pid > 0)
      killpg(pid, SIGTSTP);
    _resetTerminal(0);
    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    action.sa_flags   = SA_RESTART;
    sigaction(SIGTSTP, &action, &old);
    raise(SIGTSTP);
    sigaction(SIGTSTP, &old, NULL);
    initTerminal(pty);
    if (pid > 0)
      kill(pid, SIGCONT);
    break; }
  case SIGWINCH: {
    struct winsize win;

    if (ioctl(1, TIOCGWINSZ, &win) >= 0 &&
        win.ws_col > 0 && win.ws_row > 0) {
      int i;
      for (i = 0; i < sizeof(screenBuffer)/sizeof(ScreenBuffer *); i++)
        screenBuffer[i] = adjustScreenBuffer(screenBuffer[i],
					     win.ws_col, win.ws_row);
      currentBuffer     = screenBuffer[currentPage];
      screenWidth       = win.ws_col;
      screenHeight      = win.ws_row;
      displayCurrentScreenBuffer();
      ioctl(pty, TIOCSWINSZ, &win);
    }
    break; }
  default:
    break;
  }
  return;
}


static int emulator(int pid, int pty, int *status) {
  struct pollfd descriptors[2];
  sigset_t      unblocked, blocked;
  char          buffer[8192];
  int           count, i;
  int           discardEmptyMsg= streamsIO;

  descriptors[0].fd            = 0;
  descriptors[0].events        = POLLIN;
  descriptors[1].fd            = pty;
  descriptors[1].events        = POLLIN;
  sigemptyset(&unblocked);

  for (;;) {
    int signal                 = sigsetjmp(mainJumpBuffer,1);
    if (signal != 0)
      processSignal(signal, pid, pty);
    else
      break;
  }
  for (;;) {
    if (extraDataLength > 0) {
      userInputReceived(pty, extraData, extraDataLength);
      extraDataLength          = 0;
    }

    flushConsole();
    flushUserInput(pty);

    i                          = currentKeySequence != NULL ? 200 : -1;
    sigprocmask(SIG_SETMASK, &unblocked, &blocked);
    i                          = poll(descriptors, 2, i);
    sigprocmask(SIG_SETMASK, &blocked, NULL);

    kill(pid, SIGCONT);

    if (i < 0) {
      if (errno != EINTR)
        break;
    } else if (i == 0) {
      if (currentKeySequence != NULL) {
        i                      = strlen(currentKeySequence->nativeKeys);
        if (i > 1)
          sendUserInput(pty, currentKeySequence->nativeKeys, i - 1);
        currentKeySequence = NULL;
      }
    } else {
      int keyboardEvents       = 0;
      int ptyEvents            = 0;

      if (descriptors[0].revents) {
        keyboardEvents         = descriptors[0].revents;
        i--;
      }
      if (i > 0)
        ptyEvents              = descriptors[1].revents;

      if (keyboardEvents & POLLIN) {
        if ((count             = read(0, buffer, sizeof(buffer))) > 0) {
          userInputReceived(pty, buffer, count);
        } else if (count == 0 ||
                   (count < 0 && errno != EINTR)) {
          break;
        }
      }

      if (ptyEvents & POLLIN) {
        if ((count             = read(pty, buffer, sizeof(buffer))) > 0) {
          logCharacters(1, buffer, count);
          for (i = 0; i < count; i++)
            outputCharacter(pty, buffer[i]);
        } else if ((count == 0 && !discardEmptyMsg) ||
                   (count < 0 && errno != EINTR)) {
          break;
        }
      }

      if ((keyboardEvents | ptyEvents) & (POLLERR|POLLHUP|POLLNVAL)) {
        break;
      }

      discardEmptyMsg          = 0;
    }
  }
  flushConsole();

  /* We get here, either because the child process terminated and in the     */
  /* process of doing so closed all its file handles; or because the child   */
  /* process just closed its file handles but continues to run (e.g. as a    */
  /* backgrounded process). In either case, we want to terminate the emulator*/
  /* but in the first case we also want to report the child's exit code.     */
  /* Try to reap the exit code, and if we can't get it immediately, hang     */
  /* around a little longer until we give up.                                */
  for (i = 15; i--; ) {
    switch (waitpid(pid, status, WNOHANG)) {
    case -1:
      if (errno != EINTR)
        return(-1);
      break;
    case 0:
      break;
    default:
      return(WIFEXITED(*status) ? WEXITSTATUS(*status) : -1);
    }
    poll(0, 0, 100);
  }
  return(-1);
}


static int forkPty(int *fd, char *name) {
  int            master, slave, pid;
  struct winsize win;

  /* Try to let the standard C library open a pty pair for us                */
#if HAVE_GRANTPT
#if HAVE_GETPT
  master             = getpt();
#else
  master             = open("/dev/ptmx", O_RDWR);
#endif
  if (master >= 0) {
    grantpt(master);
    unlockpt(master);
    strcpy(name, ptsname(master));
    slave            = open(name, O_RDWR|O_NOCTTY);
    if (slave < 0) {
      close(master);
      return(-1);
    }
  }
  if (master < 0)
#endif
  {
    char         fname[40];
    char         *ptr1,*ptr2;
    int          ttyGroup;
    struct group *group;
  
    oldStylePty      = 1;
    if ((group       = getgrnam("tty")) != NULL)
      ttyGroup       = group->gr_gid;
    else
      ttyGroup       = -1;
    strcpy(fname, "/dev/ptyXX");
    master           =
      slave          = -1;
    for (ptr1        = "pqrstuvwxyzabcde"; *ptr1; ptr1++) {
      fname[8]       = *ptr1;
      for (ptr2      = "0123456789abcdef"; *ptr2; ptr2++) {
        fname[9]     = *ptr2;
        if ((master  = open(fname, O_RDWR, 0)) < 0) {
          if (errno == ENOENT) {
            return(-1);
          }
        } else {
          fname[5]   = 't';
  
          /* Old-style ptys require updating of permissions                  */
          assertPrivileges();
          chown(fname, uid, ttyGroup);
          chmod(fname, S_IRUSR|S_IWUSR|S_IWGRP);
          dropPrivileges();
          if ((slave = open(fname, O_RDWR|O_NOCTTY)) >= 0) {
            strcpy(name, fname);
            goto foundPty;
          } else {
            assertPrivileges();
            chmod(fname, 0666);
            chown(fname, 0, ttyGroup);
            dropPrivileges();
            close(master);
            fname[5] = 'p';
          }
        }
      }
    }
  }
  if (master < 0) {
    errno            = ENOENT; /* out of ptys */
    return(1);
  }
 foundPty:

#if HAVE_STROPTS_H
  if ((ioctl(slave, I_PUSH, "ptem")   |
       ioctl(slave, I_PUSH, "ldterm") |
       ioctl(slave, I_PUSH, "ttcompat")) == 0)
    streamsIO        = 1;
#endif

  /* Set new window size                                                     */
  if (ioctl(1, TIOCGWINSZ, &win) < 0 ||
      win.ws_col <= 0 || win.ws_row <= 0) {
    failure(127, "Cannot determine terminal size");
  }
  ioctl(slave, TIOCSWINSZ, &win);

  /* Now, fork off the child process                                         */
  if ((pid           = fork()) < 0) {
    close(slave);
    close(master);
    return(-1);
  } else if (pid == 0) {
    int i;

    /* Close all file handles                                                */
    closelog();
    for (i           = sysconf(_SC_OPEN_MAX); --i > 0;)
      if (i != slave)
        close(i);

    /* Become the session/process-group leader                               */
    setsid();
    setpgid(0, 0);

    /* Redirect standard I/O to the pty                                      */
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);

    /* Force the pty to be our control terminal                              */
    close(open(name, O_RDWR));
#if HAVE_TIOCSCTTY
    ioctl(slave, TIOCSCTTY, NULL);
#endif
    tcsetpgrp(slave, getpid());
    setpgid(0, 0);
    setsid();

    if (slave > 2)
      close(slave);

    return(0);
  }
  *fd                = master;
  close(slave);
  return(pid);
}


static void releasePty(void) {
  if (*ptyName && oldStylePty) {
    int          ttyGroup;
    struct group *group;
  
    if ((group = getgrnam("tty")) != NULL)
      ttyGroup = group->gr_gid;
    else
      ttyGroup = -1;
    assertPrivileges();
    chmod(ptyName, 0666);
    chown(ptyName, 0, ttyGroup);
    dropPrivileges();
  }
  return;
}


static void tstpHandler(int signalNumber) {
  int              pid  = getpid();
  int              ppid = getppid();

  kill(ppid, SIGTSTP);
  killpg(pid, SIGSTOP);
  killpg(pid, SIGCONT);
  return;
}


static void execChild(int noPty, char *argv[]) {
  char *shell, *appName, *ptr;

  needsReset                      = 0;

  if (!noPty) {
    struct winsize   win;
    char             termEnvironment[80];
    char             linesEnvironment[80];
    char             columnsEnvironment[80];

    if (ioctl(1, TIOCGWINSZ, &win) < 0 ||
        win.ws_col <= 0 || win.ws_row <= 0) {
      failure(127, "Cannot determine terminal size");
    }

    /* Configure environment variables                                       */
    snprintf(termEnvironment,    sizeof(termEnvironment),
             "TERM=%s",    cfgTerm);
    snprintf(linesEnvironment,   sizeof(linesEnvironment),
             "LINES=%d",   win.ws_row);
    snprintf(columnsEnvironment, sizeof(columnsEnvironment),
             "COLUMNS=%d", win.ws_col);
    putenv(termEnvironment);
    putenv(linesEnvironment);
    putenv(columnsEnvironment);

    /* Set initial terminal settings                                         */
    defaultTermios.c_iflag        = TTYDEF_IFLAG & ~ISTRIP;
    defaultTermios.c_oflag        = TTYDEF_OFLAG;
    defaultTermios.c_lflag        = TTYDEF_LFLAG;
    defaultTermios.c_cflag        =(TTYDEF_CFLAG & ~(CS7|PARENB|HUPCL)) | CS8;
  
    tcsetattr(0, TCSANOW, &defaultTermios);
    ioctl(0, TIOCSWINSZ, &win);
  }

#if HAVE_UNSETENV
  unsetenv("IFS");
#endif

#ifdef DEBUG_LOG_SESSION
  {char *logger                   = getenv("WY60REPLAY");
  if (logger) {
    int logFd                     = open(logger, O_RDONLY);
    if (logFd >= 0) {
      int header[4];

      while (read(logFd, header, sizeof(header)) == sizeof(header)) {
        int delay10ths;

        if (ntohl(header[0]) != sizeof(header)) {
          failure(127, "Unknown header format");
        }

        delay10ths                = ntohl(header[3]);
        if (delay10ths > 5)
          delay10ths              = 5;
        poll(0, 0, delay10ths*100);
        if (ntohl(header[2]) == 0) {
          lseek(logFd, ntohl(header[1]) - ntohl(header[0]), SEEK_CUR);
        } else {
          char buffer[1024];
          int  len                = ntohl(header[1]) - ntohl(header[0]);
          
          while (len > 0) {
            int count             = read(logFd, buffer, len > sizeof(buffer)
                                         ? sizeof(buffer) : len);
            if (count > 0) {
              write(1, buffer, count);
              len                -= count;
            } else {
              failure(127, "Count returned 0");
            }
          }
        }
      }
      exit(0);
    }
  } }
#endif

  /* Launch shell                                                            */
#if HAVE_UNSETENV
#ifdef DEBUG_LOG_SESSION
  unsetenv("WY60LOGFILE");
  unsetenv("WY60REPLAY");
#endif
#ifdef DEBUG_LOG_NATIVE
  unsetenv("WY60NATIVE");
#endif
#ifdef DEBUG_SINGLE_STEP
  unsetenv("WY60SINGLESTEP");
#endif
#ifdef DEBUG_DECODE
  unsetenv("WY60DECODE");
#endif
#endif
  if ((appName = shell            = commandName) == NULL) {
    shell                         = getenv("SHELL");
    if (shell == NULL)
      shell                       = cfgShell;
    appName                       = strcpy(((char *)malloc(strlen(shell)+2))+1,
                                           shell);
    ptr                           = strrchr(appName, '/');
    if (ptr == NULL)
      ptr                         = appName - 1;
    *(appName                     = ptr)
                                  = '-';
    if (!loginShell)
      appName++;
  }
  argv[0]                         = appName;

  if (!noPty && jobControl == J_ON) {
    int              pid;
    int              status;
    struct sigaction action;

    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    switch (pid                   = fork()) {
    case -1: /* failure */
      failure(127, "Could not execute \"%s\"\n", shell);
    case 0:  /* child */
      execvp(shell, argv);
      failure(127, "Could not execute \"%s\"\n", shell);
    default: /* parent */
      memset(&action, 0, sizeof(action));
      action.sa_handler           = tstpHandler;
      action.sa_flags             = SA_RESTART;
      sigaction(SIGTSTP, &action, NULL);
      if (waitpid(pid, &status, 0) >= 0) {
        if (WIFEXITED(status))
          exit(WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
          exit(125);
      }
      break;
    }
    exit(125);
  } else {
    execvp(shell, argv);
    failure(127, "Could not execute \"%s\"\n", shell);
  }
}


static int launchChild(char *argv[], int *pty, char *ptyName) {
  int              pid;
  struct sigaction tstpAction;

  if (jobControl != J_OFF) {
    sigaction(SIGTSTP, NULL, &tstpAction);
    jobControl = tstpAction.sa_handler == SIG_IGN ? J_OFF : J_ON;
  }  
  pid          = forkPty(pty, ptyName);
  if (pid < 0) {
    failure(127, "Failed to fork child process");
  } else if (pid == 0) {
    execChild(0, argv);
  }
  return(pid);
}


static void signalHandler(int signalNumber) {
  if (signalNumber != SIGCHLD)
    if (useAuxiliarySignalHandler)
      siglongjmp(auxiliaryJumpBuffer, signalNumber);
    else
      siglongjmp(mainJumpBuffer, signalNumber);
  return;
}


static void initSignals(void) {
  static int       signals[]            = { SIGHUP, SIGINT, SIGQUIT, SIGILL,
                                            SIGTRAP, SIGABRT, SIGBUS, SIGFPE,
                                            SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE,
                                            SIGALRM, SIGTERM, SIGCHLD, SIGXCPU,
                                            SIGXFSZ, SIGVTALRM, SIGPROF,
                                            SIGWINCH, SIGIO
                                          };
  int              i;
  struct sigaction action;
  sigset_t         blocked;

  /* We want to poll all signals in order to avoid race conditions; we must  */
  /* handle all signals that cause a program termination, so that we can     */
  /* clean up before the program exits (i.e. run the atexit() handler).      */
  memset(&action, 0, sizeof(action));
  *((void (**)(int))&action.sa_handler) = signalHandler;
  action.sa_flags                       = SA_RESTART;
  sigemptyset(&blocked);
  for (i = 0; i < sizeof(signals)/sizeof(int); i++) {
    if (signals[i] != SIGCHLD)
      sigaddset(&blocked, signals[i]);
    sigaction(signals[i], &action, NULL);
  }
  sigprocmask(SIG_BLOCK, &blocked, NULL);

  if (jobControl == J_ON) {
    sigaddset(&blocked, SIGTSTP);
    sigaction(SIGTSTP, &action, NULL);
    sigprocmask(SIG_BLOCK, & blocked, NULL);
  } else {
    action.sa_handler                   = SIG_IGN;
    sigaction(SIGTSTP, &action, NULL);
  }

  return;
}


static int setVariable(const char *key, const char *value) {
  static const struct table {
    const char *name;
    char       **variable;
  } table[] = {
    { "TERM",                &cfgTerm },
    { "SHELL",               &cfgShell },
    { "RESIZE",              &cfgResize },
    { "WRITEPROTECT",        &cfgWriteProtect },
    { "A1",                  &cfgA1 },
    { "A3",                  &cfgA3 },
    { "B2",                  &cfgB2 },
    { "C1",                  &cfgC1 },
    { "C3",                  &cfgC3 },
    { "Backspace",           &cfgBackspace },
    { "Backtab",             &cfgBacktab },
    { "Begin",               &cfgBegin },
    { "Cancel",              &cfgCancel },
    { "Clear",               &cfgClear },
    { "Clear All Tabs",      &cfgClearAllTabs },
    { "Clear Tab",           &cfgClearTab },
    { "Close",               &cfgClose },
    { "Command",             &cfgCommand },
    { "Copy",                &cfgCopy },
    { "Create",              &cfgCreate },
    { "Delete",              &cfgDelete },
    { "Delete Line",         &cfgDeleteLine },
    { "Down",                &cfgDown },
    { "End",                 &cfgEnd },
    { "End Of Line",         &cfgEndOfLine },
    { "End Of Screen",       &cfgEndOfScreen },
    { "Enter",               &cfgEnter },
    { "Exit",                &cfgExit },
    { "Exit Insert Mode",    &cfgExitInsertMode },
    { "Find",                &cfgFind },
    { "Help",                &cfgHelp },
    { "Home",                &cfgHome },
    { "Insert",              &cfgInsert },
    { "Insert Line",         &cfgInsertLine },
    { "Left",                &cfgLeft },
    { "Lower Left",          &cfgLowerLeft },
    { "Mark",                &cfgMark },
    { "Message",             &cfgMessage },
    { "Move",                &cfgMove },
    { "Next",                &cfgNext },
    { "Open",                &cfgOpen },
    { "Options",             &cfgOptions },
    { "Page Down",           &cfgPageDown },
    { "Page Up",             &cfgPageUp },
    { "Previous",            &cfgPrevious },
    { "Print",               &cfgPrint },
    { "Redo",                &cfgRedo },
    { "Reference",           &cfgReference },
    { "Refresh",             &cfgRefresh },
    { "Replace",             &cfgReplace },
    { "Restart",             &cfgRestart },
    { "Resume",              &cfgResume },
    { "Right",               &cfgRight },
    { "Save",                &cfgSave },
    { "Scroll Down",         &cfgScrollDown },
    { "Scroll Up",           &cfgScrollUp },
    { "Select",              &cfgSelect },
    { "Set Tab",             &cfgSetTab },
    { "Suspend",             &cfgSuspend },
    { "Undo",                &cfgUndo },
    { "Up",                  &cfgUp },
    { "Shift Begin",         &cfgShiftBegin },
    { "Shift Cancel",        &cfgShiftCancel },
    { "Shift Command",       &cfgShiftCommand },
    { "Shift Copy",          &cfgShiftCopy },
    { "Shift Create",        &cfgShiftCreate },
    { "Shift Delete",        &cfgShiftDelete },
    { "Shift Delete Line",   &cfgShiftDeleteLine },
    { "Shift End",           &cfgShiftEnd },
    { "Shift End Of Line",   &cfgShiftEndOfLine },
    { "Shift Exit",          &cfgShiftExit },
    { "Shift Find",          &cfgShiftFind },
    { "Shift Help",          &cfgShiftHelp },
    { "Shift Home",          &cfgShiftHome },
    { "Shift Insert",        &cfgShiftInsert },
    { "Shift Left",          &cfgShiftLeft },
    { "Shift Message",       &cfgShiftMessage },
    { "Shift Move",          &cfgShiftMove },
    { "Shift Next",          &cfgShiftNext },
    { "Shift Options",       &cfgShiftOptions },
    { "Shift Previous",      &cfgShiftPrevious },
    { "Shift Print",         &cfgShiftPrint },
    { "Shift Redo",          &cfgShiftRedo },
    { "Shift Replace",       &cfgShiftReplace },
    { "Shift Resume",        &cfgShiftResume },
    { "Shift Right",         &cfgShiftRight },
    { "Shift Save",          &cfgShiftSave },
    { "Shift Suspend",       &cfgShiftSuspend },
    { "Shift Undo",          &cfgShiftUndo },
    { "F0",                  &cfgF0 },
    { "F1",                  &cfgF1 },
    { "F2",                  &cfgF2 },
    { "F3",                  &cfgF3 },
    { "F4",                  &cfgF4 },
    { "F5",                  &cfgF5 },
    { "F6",                  &cfgF6 },
    { "F7",                  &cfgF7 },
    { "F8",                  &cfgF8 },
    { "F9",                  &cfgF9 },
    { "F10",                 &cfgF10 },
    { "F11",                 &cfgF11 },
    { "F12",                 &cfgF12 },
    { "F13",                 &cfgF13 },
    { "F14",                 &cfgF14 },
    { "F15",                 &cfgF15 },
    { "F16",                 &cfgF16 },
    { "F17",                 &cfgF17 },
    { "F18",                 &cfgF18 },
    { "F19",                 &cfgF19 },
    { "F20",                 &cfgF20 },
    { "F21",                 &cfgF21 },
    { "F22",                 &cfgF22 },
    { "F23",                 &cfgF23 },
    { "F24",                 &cfgF24 },
    { "F25",                 &cfgF25 },
    { "F26",                 &cfgF26 },
    { "F27",                 &cfgF27 },
    { "F28",                 &cfgF28 },
    { "F29",                 &cfgF29 },
    { "F30",                 &cfgF30 },
    { "F31",                 &cfgF31 },
    { "F32",                 &cfgF32 },
    { "F33",                 &cfgF33 },
    { "F34",                 &cfgF34 },
    { "F35",                 &cfgF35 },
    { "F36",                 &cfgF36 },
    { "F37",                 &cfgF37 },
    { "F38",                 &cfgF38 },
    { "F39",                 &cfgF39 },
    { "F40",                 &cfgF40 },
    { "F41",                 &cfgF41 },
    { "F42",                 &cfgF42 },
    { "F43",                 &cfgF43 },
    { "F44",                 &cfgF44 },
    { "F45",                 &cfgF45 },
    { "F46",                 &cfgF46 },
    { "F47",                 &cfgF47 },
    { "F48",                 &cfgF48 },
    { "F49",                 &cfgF49 },
    { "F50",                 &cfgF50 },
    { "F51",                 &cfgF51 },
    { "F52",                 &cfgF52 },
    { "F53",                 &cfgF53 },
    { "F54",                 &cfgF54 },
    { "F55",                 &cfgF55 },
    { "F56",                 &cfgF56 },
    { "F57",                 &cfgF57 },
    { "F58",                 &cfgF58 },
    { "F59",                 &cfgF59 },
    { "F60",                 &cfgF60 },
    { "F61",                 &cfgF61 },
    { "F62",                 &cfgF62 },
    { "F63",                 &cfgF63 } };
  int i;

  for (i = 0; i < sizeof(table) / sizeof(struct table); i++) {
    if (!strcasecmp(table[i].name, key)) {
      *table[i].variable = strdup(value);
      return(1);
    }
  }

  return(0);
}


static int expandEscapeCodes(char *buffer) {
  char *ptr;

  for (ptr = buffer; *ptr; ptr++) {
    if (*ptr == '\\') {
      switch (ptr[1]) {
        int len;
        int value;
      case 'x':
        for (len = value = 0;
             len < 2 &&
               ((ptr[len+2] >= '0' && ptr[len+2] <= '9') ||
                (ptr[len+2] >= 'A' && ptr[len+2] <= 'F') ||
                (ptr[len+2] >= 'a' && ptr[len+2] <= 'f'));
             len++)
          value  = 16*value + (ptr[len+2] & 0xF) + (ptr[len+2] > '9' ? 9 : 0);

        if (!value)
          return(0);
        
        *ptr     = (char)value;
        memmove(ptr+1, ptr+len+2, strlen(ptr+len+1));
        break;
      case '0': case '1': case '2': case '3':
      case '4': case '5': case '6': case '7':
        for (len = value = 0;
             len < 3 && (ptr[len+1] >= '0' && ptr[len+1] <= '7');
             len++)
          value  = 8*value + (ptr[len+1] & 0xF);
        *ptr     = (char)value;

        if (!value)
          return(0);

        memmove(ptr+1, ptr+len+1, strlen(ptr+len));
        break;
      case 'a':
        ptr[1]   = '\x07';
        goto compact;
      case 'b':
        ptr[1]   = '\x08';
        goto compact;
      case 'e':
        ptr[1]   = '\x1B';
        goto compact;
      case 'f':
        ptr[1]   = '\x0C';
        goto compact;
      case 'n':
        ptr[1]   = '\x0A';
        goto compact;
      case 'r':
        ptr[1]   = '\x0D';
        goto compact;
      case '\t':
        ptr[1]   = '\x09';
        goto compact;
      case '\v':
        ptr[1]   = '\x0B';
        goto compact;
      case '\"':
      case '\'':
      case '\\':
      compact:
        memmove(ptr, ptr+1, strlen(ptr));
        break;
      default:
        return(0);
      }
    }
  }
  return(1);
}


static void parseConfigurationFile(const char *fileName) {
  FILE *file;

  file         = fopen(fileName, "r");
  if (file != NULL) {
    char line[1024];
    int  lineNumber;

    for (lineNumber = 1; fgets(line, sizeof(line), file); lineNumber++) {
      char *ptr, *key, *value;

      if ((ptr = strchr(line, '#')) != NULL) {
        *ptr   = '\000';
      } else
        ptr    = strrchr(line, '\000');

      while (ptr > line &&
             (ptr[-1] == ' '  || ptr[-1] == '\t' ||
              ptr[-1] == '\r' || ptr[-1] == '\n'))
        ptr--;
      if (ptr == line)
        continue;
      else
        *ptr   = '\000';

      for (ptr = line; *ptr == ' '  || *ptr == '\t' ||
                       *ptr == '\r' || *ptr == '\n'; ptr++);
      
      key      = ptr;

      ptr      = strchr(ptr, '=');
      if (ptr == NULL)
        failure(127, "Invalid entry \"%s\" in file \"%s\" at line %d",
                line, fileName, lineNumber);

      value    = ptr+1;

      while (ptr > key &&
             (ptr[-1] == ' '  || ptr[-1] == '\t' ||
              ptr[-1] == '\r' || ptr[-1] == '\n'))
        ptr--;
      *ptr     = '\000';

      if (!*key)
        failure(127, "Empty variable name in file \"%s\" at line %d",
                fileName, lineNumber);

      while (*value == ' '  || *value == '\t' ||
             *value == '\r' || *value == '\n')
        value++;

      if (!expandEscapeCodes(value))
        failure(127, "Illegal escape sequence in entry \"%s\" in file \"%s\""
                " at line %d", value, fileName, lineNumber);

      if (!setVariable(key, value))
        failure(127, "Unknown variable \"%s\" in file \"%s\" at line %d",
                key, fileName, lineNumber);
    }
    fclose(file);
  }
  return;
}


static void parseConfigurationFiles(void) {
  char *home;

  parseConfigurationFile(ETCDIR"/wy60.rc");
  parseConfigurationFile("/etc/wy60.rc");
  home                 = getenv("HOME");
  if (home != NULL) {
    char *wy60rc       = strcat(strcpy(malloc(strlen(home) + 20), home),
                                "/.wy60rc");

    parseConfigurationFile(wy60rc);
    free(wy60rc);
  }

  if (cfgWriteProtect && *cfgWriteProtect) {
    static const struct lookup {
      const char *name;
      int   value; } lookup[] = { { "NORMAL",     T_NORMAL     },
                                  { "BLANK",      T_BLANK      },
                                  { "BLINK",      T_BLINK      },
                                  { "REVERSE",    T_REVERSE    },
                                  { "UNDERSCORE", T_UNDERSCORE },
                                  { "DIM",        T_DIM        } };
    const char *ptr, *end;
    int  index, attribute     = 0;

    protectedPersonality      = T_NORMAL;
    for (ptr = cfgWriteProtect; *ptr; ) {
      while (*ptr && (*ptr < 'A' || (*ptr > 'Z' && *ptr < 'a') || *ptr > 'z'))
        ptr++;
      for (end = ptr; *end && ((*end >= 'A' && *end <= 'Z') ||
                               (*end >= 'a' && *end <= 'z')); end++);
      if (ptr == end)
        break;
      for (index = sizeof(lookup)/sizeof(struct lookup); index--; ) {
        const char *src, *dst;
        for (src = ptr, dst = lookup[index].name;; src++, dst++) {
          if (src == end) {
            if (!*dst) {
              attribute       = lookup[index].value;
              goto foundAttribute;
            }
            break;
          } else if (!*dst) {
            break;
          } else if ((*src ^ *dst) & ~0x20) {
            break;
          }
        }
      }
    foundAttribute:
      if (index >= 0) {
        protectedPersonality |= attribute;
      } else {
        failure(127, "Cannot parse write-protected attributes: \"%s\"\n",
                cfgWriteProtect);
      }
      ptr                     = end;
    }
    protectedAttributes       = protectedPersonality;
  }
  return;
}


static void help(char *applicationName) {
  printf("Usage: %s [-c | --command <cmd>] [ -h | --help ] [ -l | --login ]\n"
         "\t[-t | --term <terminal>] [ -v | --version ] [ -- ] <shell args>\n",
         applicationName);
  exit(0);
}


static void version(void) {
  printf("%s\n", WY60_VERSION);
  exit(0);
}


static char **parseArguments(int argc, char *argv[]) {
  static const struct option {
    const char *name;
    int        has_arg;
    int        *flag;
    int        val;
  } longOpts[] = {
    { "command",     1, NULL, 'c' },
    { "help",        0, NULL, 'h' },
    { "job-control", 1, NULL, 'j' },
    { "login",       0, NULL, 'l' },
    { "term",        1, NULL, 't' },
    { "version",     0, NULL, 'v' },
    { NULL,          0, NULL, 0 } };
  static const char *optString    = "c:hj:lt:v";
  int               argumentIndex = 1;
  char              ch, *arg      = argv[argumentIndex];
  int               state         = 0;
  const char        *ptr, *shell;
  char              *parameter;
  int               i;

  /* This emulator is a wrapper for a login shell if either the application  */
  /* name starts with a minus character, or the application name is          */
  /* identical to the value of the "SHELL" environment variable.             */
  if (*argv &&
      (!!(loginShell              = (*argv[0] == '-')) ||
       ((shell                    = getenv("SHELL")) != NULL &&
       !strcmp(shell, argv[0])) ||
       (shell != NULL && (shell   = strrchr(shell, '/')) != NULL &&
        !strcmp(shell+1, argv[0])))) {
    isLoginWrapper                = 1;
    return(argv);
  }

  if (!arg) {
    *argv                         = cfgShell;
    return(argv);
  }

  while ((ch = *arg) != '\000') {
    parameter                     = NULL;

    switch (state) {
    case 0:
      if (ch == '-')
        state                     = 1;
      else {
        argv                     += argumentIndex - 1;
        *argv                     = cfgShell;
        return(argv);
      }
      break;
    case 1:
      if (*arg == '-') {
        state                     = 3;
        break;
      } else
        state                     = 2;
      /* fall thru */
    case 2:
      for (ptr = optString; *ptr && *ptr != ch; ptr++)
        if (ptr[1] == ':')
          ptr++;
      if (*ptr) {
        if (ptr[1] == ':') {
          if (arg[1]) {
            parameter             = arg + 1;
            arg                   = "";
          } else {
            if ((parameter = argv[++argumentIndex]) == NULL)
              failure(1, "Insufficient arguments for \"-%c\"\n", ch);
            arg                   = "";
          }
        }
      } else {
        if ( argv[argumentIndex]+1 == arg ||
            (argv[argumentIndex]+2 == arg && arg[-1] == '-')) {
          argv                   += argumentIndex - 1;
          *argv                   = cfgShell;
          return(argv);
        } else
          failure(1, "Illegal command line argument \"%s\"",
                  argv[argumentIndex]);
      }
      break;
    case 3:
      for (i = 0; longOpts[i].name; i++)
        if (!strcmp(arg, longOpts[i].name))
          break;

      if (longOpts[i].name) {
        if (longOpts[i].has_arg) {
          if ((parameter = argv[++argumentIndex]) == NULL)
            failure(1, "Insufficient arguments for \"--%s\"\n", arg);
          arg                     = "";
        }
      } else {
        argv                     += argumentIndex - 1;
        *argv                     = cfgShell;
        return(argv);
      }
      state                       = 0;
      break;
    }

    switch (ch) {
    case '-':
      break;
    case 'c':
      commandName                 = parameter;
      break;
    case 'h':
      help(argv[0]);
      break;
    case 'j':
      if (!strcmp(parameter, "on"))
        jobControl                = J_ON;
      else if (!strcmp(parameter, "off"))
        jobControl                = J_OFF;
      else
        failure(1, "Job control can be either \"on\" or \"off\"; "
                   "unknown argument: \"%s\"\n", parameter);
      break;
    case 'l':
      loginShell                  = 1;
      break;
    case 't':
      cfgTerm                     = parameter;
      break;
    case 'v':
      version();
      break;
    }

    if (!*arg || !*++arg) {
      if (state == 1 || state == 3 ||
          (arg = argv[++argumentIndex]) == NULL) {
        argv                     += argumentIndex - (state == 3 ? 0 : 1);
        *argv                     = cfgShell;
        return(argv);
      }

      state                       = 0;
    }
  }
  
  argv                           += argumentIndex - 1;
  *argv                           = cfgShell;

  return(argv);
}


int main(int argc, char *argv[]) {
  int  pid, pty, status;
  char **extraArguments;

  dropPrivileges();
  parseConfigurationFiles();
  extraArguments     = parseArguments(argc, argv);

  /* If we were called as a wrapper for a login shell we must make sure to   */
  /* break the loop of calling ourselves again. Reset the value of the SHELL */
  /* environment variable, and depending on whether we already run in an     */
  /* emulation decide to just replace ourselves with the shell process rather*/
  /* than invoking it as a child process.                                    */
  if (isLoginWrapper) {
    char shellEnvironment[80];
    char *oldTerminal;

    snprintf(shellEnvironment, sizeof(shellEnvironment), "SHELL=%s", cfgShell);
    putenv(shellEnvironment);
    
    if ((oldTerminal = getenv("TERM")) != NULL &&
        !strcmp(oldTerminal, cfgTerm))
      execChild(1, argv);
  }
    
  initTerminal(-1);
  initKeyboardTranslations();
  pid                = launchChild(extraArguments, &pty, ptyName);
  atexit(resetTerminal);
  atexit(releasePty);
  initSignals();
  if (emulator(pid, pty, &status) < 0)
    return(125);
  return(WIFEXITED(status) ? WEXITSTATUS(status) : 125);
}
