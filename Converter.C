#ifdef _WIN32
        #include <windows.h>
#else
        #include <unistd.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <stdexcept>
#include <dirent.h>
#include <lame/lame.h>
#define WAV_SIZE 10240
#define MP3_SIZE 10240
#include <fstream>
using namespace std;

int channel;
ofstream logFile;

typedef struct converterThreadData {
  string fileName;
  int *threadCount;
  pthread_mutex_t * mutex;
}
converterThreadData;

enum logMode {
  Error,
  INFO,
  DEBUG
};

logMode loggerMode;

logMode convertLoggerMODE(string mode)
{
  if(mode.compare("Error") == 0)
    return Error;
  else if(mode.compare("INFO") == 0)
    return INFO;
  else if(mode.compare("DEBUG") == 0)
    return DEBUG;
  return Error;
}
void logger(logMode mode, string logStatement) {
  if(mode <= loggerMode)
    logFile<<logStatement<<endl;
}
int getCoreCount() {
  #ifdef _WIN32
    SYSTEM_INFO windSysInfo;
    GetSystemInfo( & windSysInfo);
    return windSysInfo.dwNumberOfProcessors;
  #else
    return sysconf(_SC_NPROCESSORS_ONLN);
  #endif
}
void goToSleep(int time) {
  #ifdef _WIN32
    Sleep(1000*time);
  #else
    sleep(time);
  #endif
}
int changeToMP3(string fileName, pthread_mutex_t * mutex)
{

  lame_global_flags * lameflags;
  FILE * wavfile, * mp3file;
  size_t bytesread = 1, byteswrote;
  long count = 0;
  short * leftPadding, *rightPadding;

  string destFile = fileName.substr(0, fileName.length() - 3) + "mp3";
  logger(INFO,"dest file "+destFile);
  lameflags = lame_init();
  if (lame_init_params(lameflags) < 0)
  {
    if (pthread_mutex_lock(mutex) == 0)
    {
      fprintf(stderr, "Error while setting internal parameters\n");

      pthread_mutex_unlock(mutex);
    }
  }
  if ((wavfile = fopen(fileName.c_str(), "rb")) == NULL)
  {
    if (pthread_mutex_lock(mutex) == 0)
    {
      pthread_mutex_unlock(mutex);
    }

    return (EXIT_FAILURE);
  }
  if ((mp3file = fopen(destFile.c_str(), "wb")) == NULL)
  {
    if (pthread_mutex_lock(mutex) == 0)
    {
      pthread_mutex_unlock(mutex);
    }

    return (EXIT_FAILURE);
  }

  short * wavBuffer = new short[sizeof(short) * 2 * WAV_SIZE];
  unsigned char * mp3Buffer = new unsigned char[sizeof(unsigned char) * MP3_SIZE];

  while (bytesread != 0)
  {

    bytesread = fread(wavBuffer, 2 * sizeof(short), WAV_SIZE, wavfile);
    if (bytesread != 0) {

      if (channel != 1) {
        rightPadding = new short[sizeof(short) * bytesread];
        int m = 0,n = bytesread-1;
        while(m <= n)
        {
          rightPadding[m] = wavBuffer[m];
          rightPadding[n] = wavBuffer[n];
          m++;
          n--;
        }

        leftPadding = rightPadding;
        byteswrote = lame_encode_buffer(lameflags, leftPadding, rightPadding, bytesread, mp3Buffer, MP3_SIZE);
      } else //for monostream
      {
        byteswrote = lame_encode_buffer_interleaved(lameflags, wavBuffer, bytesread, mp3Buffer, MP3_SIZE);
      }

      delete rightPadding;
      leftPadding = rightPadding = NULL;

    } else {
      byteswrote = lame_encode_flush(lameflags, mp3Buffer, MP3_SIZE);
    }
    if (byteswrote < 0) {
      if (pthread_mutex_lock(mutex) == 0) {
        fprintf(stderr, "Error during encoding, byteswrote: %ld\n", byteswrote);

        pthread_mutex_unlock(mutex);
      }
      return (EXIT_FAILURE);
    }
    fwrite(mp3Buffer, byteswrote, 1, mp3file);

  }
  lame_close(lameflags);

  delete wavBuffer;
  delete mp3Buffer;

  wavBuffer = NULL;
  mp3Buffer = NULL;

  fclose(wavfile);
  fclose(mp3file);
  return (EXIT_SUCCESS);
}
void * threadHelper(void * params)
{
  struct converterThreadData * args = ((converterThreadData * )(params));
  char * compliance;
  logger(INFO,"threadHelper");
  if (changeToMP3(args -> fileName, args -> mutex) != 0)
  {
    logger(Error,"failed in changeToMP3");
    strcpy(compliance, "FAILED,in converter");
    pthread_exit(compliance);
  }
  logger(INFO,"changeToMP3 completed");
  if (pthread_mutex_lock(args -> mutex) == 0)
  {
    if ( *args -> threadCount > 0)
    {
      *args->threadCount = *args->threadCount - 1;
    }
    pthread_mutex_unlock(args -> mutex);
  }
  if (pthread_mutex_lock(args -> mutex) == 0) {
    pthread_mutex_unlock(args -> mutex);
  }
  free(args);
  pthread_exit(compliance);
}

int main(int argc, char ** argv) {

  if (argc < 2) {
    cout << "Usage\n./converter directory-path debug-mode monochrome" << endl;
    cout << "debug-mode, supported values:Error,INFO,DEEBUG" << endl;
    exit(1);
  }

  string path = argv[1];
  loggerMode = Error;
  if(argc == 3)
  {
    loggerMode = convertLoggerMODE(argv[2]);
  }

  logFile.open("converter.log");

  if(argc == 4)
    channel = atoi(argv[3]);
  else
    channel = 0;

  int lThreadCount = 0, coreCount = getCoreCount();

  if (coreCount <= 0) {
    coreCount = 1;
    logger(Error, "error with thread count assigning -1");
  }
  DIR * srcDir;
  struct dirent * ent;
  pthread_mutex_t mutexLocal;
  pthread_mutex_init( & mutexLocal, NULL);
  string delimeter("//");

  #ifdef _WIN32
    delimeter = "\\";
  #endif

  srcDir = opendir(path.c_str());
  if (srcDir != NULL)
   {
    while ((ent = readdir(srcDir)) != NULL)
    {
      string fileName(ent -> d_name);

      if (fileName.find(".wav") == std::string::npos || !fileName.compare(".") || !fileName.compare("..") || opendir(ent -> d_name) != NULL)
      {
        logger(INFO,string("skipping ") + string(ent->d_name));
        continue;
      }
      else
        logger(INFO,string("Processing ") + string(ent->d_name));


      pthread_t thread;
      converterThreadData * fileConverter = new converterThreadData();
      fileConverter->fileName = path + delimeter  + string(ent -> d_name);
      fileConverter->mutex = &mutexLocal;
      fileConverter->threadCount = &lThreadCount;
      while (lThreadCount >= coreCount)
      {
        goToSleep(1);
      }
      logger(INFO,"creating thread");
      if (pthread_create( & thread, NULL, threadHelper, fileConverter) != 0)
      {
        logger(Error,"FAILED:pthread_create");
        pthread_cancel(thread);
      }
      else
      {
        if (pthread_mutex_lock( & mutexLocal) == 0)
        {
          lThreadCount++;
          pthread_mutex_unlock( & mutexLocal);
        }
        if (pthread_detach(thread) != 0)
        {
          logger(Error, "FAILED:pthread_detach");
        }
      }
    }
    while(lThreadCount > 0)
    {
      //cout<<endl<<"threadCount "<<lThreadCount;
      goToSleep(2);
    }
    closedir(srcDir);
  }
  else
  {
    logger(Error, "Error while opening directory");
  }
  logFile.close();
  cout<<endl<<"Thats All folks,Everything completed";

}