// tUtil.cpp: implementation of the tUtil class.
//
//////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <process.h>  // spawnlp
#include <limits.h>

#include "tUtil.h"
#include "tLuaCOMException.h"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

#ifndef _MSC_VER  // not MSVC++
# define _spawnlp spawnlp
#endif

#define MAX_VALID_STRING_SIZE 1000

// https://github.com/windtail/luacom/commit/3852e47c2f0fe77477cbde54541228cd2f7b8901
// - in none-english environment (i am Chinese) we DO NOT use CP_UTF8, CP_ACP should be used
// - while Cygwin default convert filename internally to UTF-8
// - we have to use ASCII format for our lua source code, if you prefer UTF-8, you need luaiconv to convert UTF-8 to your ASCII format (GBK or other)
#ifdef __CYGWIN__
__declspec(thread) UINT code_page=CP_UTF8; // By default, Cygwin internally convert filename to UTF-8
#else
__declspec(thread) UINT code_page=CP_ACP;
#endif

FILE* tUtil::log_file = NULL;
CRITICAL_SECTION log_file_cs;
volatile bool g_log_file_cs_initialized = false;
// log methods are all static; there's no clear initialization time;
// so just always check that the CS is initialized before using it
void CSInit()
{
  if(!g_log_file_cs_initialized)
  {
	  g_log_file_cs_initialized = true;
	  InitializeCriticalSection(&log_file_cs);
  }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

bool tUtil::IsValidString(LPCTSTR string)
{
  bool return_value = string != NULL;

  assert(return_value);

  return return_value;
}

tStringBuffer tUtil::GetErrorMessage(DWORD errorcode)
{
  LPSTR lpMsgBuf;
  DWORD result = 0;

  result = FormatMessageA( 
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL,
    errorcode,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
    (LPSTR)&lpMsgBuf,
    0,
    NULL);
  // note: non-Unicode (FormatMessageA not FormatMessage/LPTSTR).  How would we
  // propogate Unicode error messages to Lua?

  if(result == 0)
    return NULL;

  // Strip the newline at the end of the message

  while((result > 1) && ((lpMsgBuf[result-1] == '\n') || (lpMsgBuf[result-1] == '\r')))
    result--;
  lpMsgBuf[result] = '\0';

  tStringBuffer ret(lpMsgBuf);

  // Free the buffer.
  LocalFree( lpMsgBuf );

  return ret;
}


tStringBuffer tUtil::bstr2string(BSTR bstr, bool nullTerminated)
{
  char* str = NULL;
  size_t len = 0;
  try
  {
    if(bstr == NULL) // NULL BSTR indicates empty string.
    {
      len = 0;
      return "";
    }
    else
    {

      UINT lenWide = SysStringLen(bstr); // not including '\0' terminator
      if (lenWide > INT_MAX) LUACOM_ERROR("string too long");

      if(lenWide == 0)
      {
        len = 0;
        return "";
      }

      // gets string length
      int lenMulti = WideCharToMultiByte(
        code_page,            // code page
        0,            // performance and mapping flags
        bstr,    // wide-character string
        static_cast<int>(lenWide),  // number of chars in string
        str,     // buffer for new string
        0,          // size of buffer (0=return it)
        NULL,     // default for unmappable chars
        NULL  // set when default char used
      );

      if(!lenMulti)
        LUACOM_ERROR(tUtil::GetErrorMessage(GetLastError()));

      struct C { C(int size) { s = new char[size]; }
		~C() { delete [] s; } char * s; } str(lenMulti + (nullTerminated? 1 : 0));

      int result = WideCharToMultiByte(
        code_page,            // code page
        0,            // performance and mapping flags
        bstr,    // wide-character string
        static_cast<int>(lenWide),  // number of chars in string
        str.s,     // buffer for new string
        lenMulti,          // size of buffer
        NULL,     // default for unmappable chars
        NULL  // set when default char used
      );

      if(!result)
        LUACOM_ERROR(tUtil::GetErrorMessage(GetLastError()));
      
      if (nullTerminated) str.s[lenMulti] = '\0';
	  len = lenMulti + (nullTerminated? 1 : 0);
      return tStringBuffer(str.s, len);
    }
  }
  catch(class tLuaCOMException& e)
  {
    UNUSED(e);
    len = 0;
    return "";
  }

}

BSTR tUtil::string2bstr(const char * string, size_t len)
{
  if(!string)
    return NULL;

  try
  {
    BSTR bstr;
    if(len == 0)
    {
      bstr = SysAllocStringLen(NULL, 0);
    }
    else
    {
      if (len != -1 && len > INT_MAX) LUACOM_ERROR("string too long");
      int lenWide =
        MultiByteToWideChar(code_page, 0, string, static_cast<int>(len), NULL, 0);
      if(lenWide == 0)
        LUACOM_ERROR(tUtil::GetErrorMessage(GetLastError()));
      bstr = SysAllocStringLen(NULL, lenWide); // plus initializes '\0' terminator
      MultiByteToWideChar(  code_page, 0, string, static_cast<int>(len), bstr, lenWide);
    }
    return bstr;
  }
  catch(class tLuaCOMException& e)
  {
    UNUSED(e);
    return NULL;
  }
}

bool tUtil::OpenLogFile(const char *name)
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
  tUtil::CloseLogFile();

  tUtil::log_file = fopen(name, "w");

  if(!tUtil::log_file)
    return false;
  else
    return true;
}

void tUtil::CloseLogFile()
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
  if(tUtil::log_file)
  {
    fclose(tUtil::log_file);
    tUtil::log_file = NULL;
  }
}

void tUtil::log(const char *who, const char *what, ...)
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
  if(tUtil::log_file && who && what)
  {
    int size = 0;

    fprintf(tUtil::log_file, "%s:", who);

    va_list marker;
    va_start(marker, what);

    size = vfprintf(tUtil::log_file, what, marker);

    va_end(marker);

    if(what[strlen(what) - 1] != '\n')
      fprintf(tUtil::log_file, "\n");

    fflush(tUtil::log_file);

#ifdef VERBOSE

    char *buffer = new char[size+1];

    sprintf(buffer, "%s:", who);

    va_start(marker, what);

    size = vsprintf(buffer, what, marker);

    va_end(marker);

    MessageBoxA(NULL, buffer, "LuaCOM Log", MB_OK | MB_ICONEXCLAMATION);

    delete[] buffer;
    buffer = NULL;

#endif // VERBOSE
  }
}

void tUtil::log_verbose(const char *who, const char *what, ...)
{
  CSInit();CriticalSectionObject cs(&log_file_cs); // prevent other threads from concurrent access
#ifdef VERBOSE
  if(tUtil::log_file && who && what)
  {
    fprintf(tUtil::log_file, "%s:", who);

    va_list marker;
    va_start(marker, what);

    vfprintf(tUtil::log_file, what, marker);

    va_end(marker);

    if(what[strlen(what) - 1] != '\n')
      fprintf(tUtil::log_file, "\n");

    fflush(tUtil::log_file);
  }
#endif
}


char * tUtil::strdup(const char *string)
{
  if(!string)
    return NULL;

  char *new_string = (char *) malloc(strlen(string)+1);

  strcpy(new_string, string);

  return new_string;
}

void tUtil::ShowHelp(const char *filename, unsigned long context)
{
  // filename must have at least the extension
  if(!filename || strlen(filename) < 5)
    return;

  const char* extension = &filename[strlen(filename) - 4];

  if(_stricmp(extension, ".chm") == 0)
  {
    char context_param[50];
  
    if(context != 0)
      sprintf(context_param, "-mapid %d", context);
    else
      context_param[0] = '\0';
    _spawnlp(_P_NOWAIT, "hh.exe", "hh.exe", context_param, filename, NULL);
  }
  else if(_stricmp(extension, ".hlp") == 0)
  {
    if(context != 0)
      WinHelpA(NULL, filename, HELP_CONTEXT, context);
    else
      WinHelpA(NULL, filename, HELP_FINDER, 0);
  }
}

void tUtil::RegistrySetString(lua_State* L, const char& Key, const char* value)
{
	lua_pushlightuserdata(L, (void *)&Key);  /* push address */ 
	lua_pushstring(L, value); 
	/* registry[&Key] = value */ 
    lua_settable(L, LUA_REGISTRYINDEX); 
}

tStringBuffer tUtil::RegistryGetString(lua_State* L, const char& Key)
{
	lua_pushlightuserdata(L, (void *)&Key);  /* push address */ 
    lua_gettable(L, LUA_REGISTRYINDEX);  /* retrieve value */ 
    return tStringBuffer(lua_tostring(L, -1));  /* convert to string */
}

// VariantTime <-> SystemTime has trouble with milliseconds
//		see http://support.microsoft.com/kb/297463
//
//
// variant/system time conversions with millisecond support
// see http://www.codeproject.com/KB/datetime/SysTimeToVarTimeWMillisec.aspx
//

#define ONETHOUSANDMILLISECONDS  .0000115740740740
/*
A variant time is stored as an 8-byte real value (double), representing a date between January 1, 1753
and December 31, 2078,inclusive. The value 2.0 represents January 1, 1900; 3.0 represents January 2, 1900, and so on.
Adding 1 to the value increments the date by a day. The fractional part of the value represents the time of day.
Therefore, 2.5 represents noon on January 1, 1900; 3.25 represents 6:00 A.M. on January 2, 1900, and so on.
so 0.5 represents 12 hours ie 12*60*60 seconds, hence 1 second = .0000115740740740
*/

BOOL tUtil::VariantTimeToSystemTimeWithMilliseconds(/*input*/ double dVariantTime, /*output*/SYSTEMTIME* st)
{
    BOOL retVal = TRUE;

    double halfsecond = ONETHOUSANDMILLISECONDS / 2.0;
    // ONETHOUSANDMILLISECONDS is equal to 0.0000115740740740

    // this takes care of rounding problem with 
    if (!VariantTimeToSystemTime(dVariantTime - halfsecond, st))
        return FALSE;

    // extracts the fraction part
    double fraction = dVariantTime - (int)dVariantTime;

    double hours;
    hours = fraction = (fraction - (int)fraction) * 24;

    double minutes;
    minutes = (hours - (int)hours) * 60;

    double seconds;
    seconds = (minutes - (int)minutes) * 60;

    double milliseconds;
    milliseconds = (seconds - (int)seconds) * 1000;

    milliseconds = milliseconds + 0.5; // rounding off millisecond to the 
    // nearest millisecond 

    if (milliseconds < 1.0 || milliseconds > 999.0) //Fractional 
        // calculations may yield in results like
        milliseconds = 0; // 0.00001 or 999.9999 which should actually 
    // be zero (slightly above or below limits 
    // are actually zero)
    if (milliseconds)
        st->wMilliseconds = (WORD)milliseconds;
    else  // if there is 0 milliseconds, then we don't have the problem !!
        retVal = VariantTimeToSystemTime(dVariantTime, st); // 

    return retVal;
}

BOOL tUtil::SystemTimeToVariantTimeWithMilliseconds(/*input*/ SYSTEMTIME st, /*output*/double* dVariantTime)
{
    WORD wMilliSeconds = st.wMilliseconds; // save the milli second information

    st.wMilliseconds = 0; // pass 0 milliseconds to the function and get  
    // the converted value without milliseconds
    double dWithoutms;
    if (!SystemTimeToVariantTime(&st, &dWithoutms))
        return FALSE;

    // manually convert the millisecond information into variant 
    // fraction and add it to system converted value
    *dVariantTime = dWithoutms + (ONETHOUSANDMILLISECONDS * wMilliSeconds / 1000.0);
    return TRUE;
}