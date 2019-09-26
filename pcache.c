#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

const int SUCCESS = 0;
const int FAIL = -1;
#define PAGE_SHIFT 12
#define PAGE_MASK 0x0fff
#define MAX_TODO_LIST  0x4000
#define MAX_TODO_MASK  0x3FFF
char ToDoList[MAX_TODO_LIST][2048];
int iToDoListHead = 0;
int iToDoListTail = 0;
size_t iCachedPages = 0;
size_t iFileCnts = 0;
size_t iFilePages = 0;
size_t iFileFailed = 0;
size_t iDirCnts = 0;

int bVerbose = 0;
int bCheckCachedPages = 0;
int bWillNotNeed = 0;
int bWillNeed = 0;
int bShowMemInfo = 0;
int bProcessSubDir = 0;
//void
//ConvertToHumanReadable (long iSize, char buf[])
//{
//  const char *suffixes[] =
//    { "B", "K", "M", "G", "T", "P", "E" };
//  int s = 0;
//
//  double iCount = iSize;
//
//  while (iCount >= 1024 && s < 7)
//    {
//      ++s;
//      iCount /= 1024;
//    }
//
//  if (0.0 == iCount - floor (iCount))
//    {
//      sprintf (buf, "%d%s", (int) iCount, suffixes[s]);
//    }
//  else
//    {
//      sprintf (buf, "%.1f%s", iCount, suffixes[s]);
//    }
//}

void
OnScreen (const char *fmt, ...)
{
  if (bVerbose)
    {
      va_list ap;
      va_start(ap, fmt);
      vprintf (fmt, ap);
      va_end(ap);
    }
}

void
ShowBanner (const char *pName)
{
  const char *sBanner =
      "\nPageCached V1.0\n\n"
      "PageCached can analyze files cached in the memory by the kernel . Linux kernel 2.6 is required.\n"
      "Usage:\n"
      "%s [options] File/Directory\n\n"
      "Options:\n"
      "-c count the pages cached in memory. Page size is 4KBytes.\n"
      "-v verbose output\n"
      "-d advise kernel attempts to free cached pages associated with the specified region. Dirty pages "
      "that have not been synchronized may not work. \n"
      "-n advise kernel the specified data will be accessed in the near future.The amount of data read may be "
      "decreased by the kernel depending on virtual memory load. \n"
      "-s process subdirectories \n"
      "-h print this info.\n"
      "Hide files (start with .) will not processed.\n\n"
      "Example:\n"
      "Analyze all files in the current directory that are cached into memory.\n\n"
      "\t%s -c . \n\n"
      "Analyze files in the /home/data/history (and it's subdirectories) that are cached into memory.\n\n"
      "\t%s -cs /home/data/histories \n\n"
      "Advise the kernel do free the cache of files in the /home/data .\n\n"
      "\t%s -d /home/data \n\n"
      "Advise the kernel to cache files in the /home/data into memory. This operation may cause HIGH system I/O!\n\n"
      "\t%s -n /home/data \n\n";
  printf (sBanner, pName, pName, pName, pName, pName);

}

void
ShowStaticInfo ()
{

  printf ("Scaned %lu Directories, %lu Files(%lu Failed). ", iDirCnts,
	  iFileCnts, iFileFailed);
  if (bCheckCachedPages)
    printf ("FilePages:%lu, CachedPages:%lu(%.3f GB)", iFilePages, iCachedPages,
	    ((double) iCachedPages * 4.0) / 1024 / 1024);

  printf ("\n");
}

void
AppendToDoList (const char *name)
{
  memccpy (ToDoList[iToDoListTail++], name, 0, sizeof(ToDoList[0]) - 1);
  iToDoListTail &= MAX_TODO_MASK;
  iDirCnts++;
}

const char*
GetHead ()
{
  if (iToDoListHead == iToDoListTail)
    {
      if (bVerbose)
	OnScreen ("Directory List Water Mark:%d", iToDoListHead);
      return NULL;
    }
  const char *p = ToDoList[iToDoListHead++];
  iToDoListHead &= MAX_TODO_MASK;
  return p;
}

void
ShowErrorInfo (const char *sFilename, int LineNumber)
{
  OnScreen ("Error:%s:%d - %s\n", sFilename, LineNumber, strerror (errno));
}

int
GetCachedPages (int fd, void *ptr, size_t st_size)
{

  unsigned long pages = 0, Cached = 0;
  static unsigned char vec[0x4000000];
  int iRet = mincore (ptr, st_size, vec);
  if (iRet == FAIL)
    {
      OnScreen ("mincore error:%s", strerror (errno));

      return FAIL;
    }

  pages = st_size >> PAGE_SHIFT;
  pages += (st_size & ~PAGE_MASK) != 0;
  if (pages > 0x4000000)
    {
      OnScreen ("File is too large to analyse!\n");
      return FAIL;
    }
  for (unsigned long i = 0; i < pages; i++)
    if ((vec[i] & 1) == 1)
      Cached++;
  iCachedPages += Cached;
  iFilePages += pages;
  OnScreen ("Cached=%lu Pages", Cached);
  return SUCCESS;
}

void
DoDropCache (int fd, size_t st_size, int advice)
{
  if (posix_fadvise (fd, 0, st_size, advice) != 0)
    {
      OnScreen ("Cache posix_fadvise(%d) failed, %s\n", advice,
		strerror (errno));
    }
  return;
}

int
DealOneFile (const char *sFullPath)
{

  int iRet = SUCCESS;

  iFileCnts++;

  int fd = open (sFullPath, O_RDONLY);

  if (fd == -1)
    {
      ShowErrorInfo (__FILE__, __LINE__);
      return FAIL;
    }

  struct stat statbuf;

  if (fstat (fd, &statbuf) == FAIL)
    {
      close (fd);
      ShowErrorInfo (__FILE__, __LINE__);
      return SUCCESS;
    }

  OnScreen ("size:%ld\t", statbuf.st_size);

  if (statbuf.st_size == 0)
    {
      OnScreen ("\n");
      close (fd);
      return SUCCESS;
    }

  void *ptr;

  ptr = mmap ((void*) 0, statbuf.st_size, PROT_NONE, MAP_SHARED, fd, 0);

  if (ptr == NULL)
    {
      OnScreen ("m_ptr:%p %s\t", ptr, strerror (errno));
      iRet = FAIL;
    }


  if (iRet == SUCCESS && bCheckCachedPages)
    iRet = GetCachedPages (fd, ptr, statbuf.st_size);
  OnScreen ("\n");
  munmap (ptr, statbuf.st_size);

  //fdatasync(fd);
  if (iRet == SUCCESS && (bWillNotNeed || bWillNeed))
    DoDropCache (fd, statbuf.st_size,
		 bWillNeed ? POSIX_FADV_WILLNEED : POSIX_FADV_DONTNEED);
  close (fd);

  return iRet;
}

int
ProcessSingleDir (const char *sPath)
{
  DIR *ptrDir = NULL;
  if ((ptrDir = opendir (sPath)) == NULL)
    {
      printf ("Process [%s] Failed! \t", sPath);
      ShowErrorInfo (__FILE__, __LINE__);
      printf ("\n");
      return FAIL;
    }
  OnScreen ("ProcessSingleDir:%s\n", sPath);

  struct dirent *pFile = NULL;
  while ((pFile = readdir (ptrDir)) != NULL)
    {
      OnScreen ("%s/%s[%d]\t", sPath, pFile->d_name, pFile->d_type);
      if (pFile->d_name[0] == '.')
	{
	  OnScreen ("\n");
	  continue;
	}
      char sFullPath[2048];

      char *p = (char*) memccpy (sFullPath, sPath, 0, sizeof(sFullPath));
      if (p)
	*(p - 1) = '/';
      p = memccpy (p, pFile->d_name, 0, sizeof(sFullPath) - (p - sFullPath));

      if (!p || p - sFullPath >= sizeof(sFullPath))
	{
	  printf ("Process [%s/%s] Failed!  Path is too long!\n", sPath,
		  pFile->d_name);
	  continue;
	}

      switch (pFile->d_type)
      {
	case DT_REG:
	  if (DealOneFile (sFullPath) == FAIL)
	    iFileFailed++;
	  if (iFileCnts % 5000 == 0)
	    ShowStaticInfo ();
	  break;
	case DT_DIR:
	  {
	    OnScreen ("\n");
	    if (bProcessSubDir)
	      AppendToDoList (sFullPath);
	    break;
	  }
	  break;
	default:
	  break;

      }
    };
  closedir (ptrDir);
  return SUCCESS;
}

int
CheckOpt (char *argv[])
{
  int bErrorOpt = 0;

  if (bCheckCachedPages == 0 && bWillNotNeed == 0 && bWillNeed == 0)
    {
      ShowBanner ("pcache");
      bErrorOpt = 1;
    }
  if (bWillNotNeed && bWillNeed)
    {
      printf ("-d and -n cannot use together!\n");
      bErrorOpt = 1;
    }
  return bErrorOpt;
}

void
ShowMemInfo ()
{

  FILE *fp = fopen ("/proc/meminfo", "r");
  if (fp == NULL)
    {
      ShowErrorInfo (__FILE__, __LINE__);
      return;
    }
  char buffer[5][64];
  for (int i = 0; i < 5; i++)
    {
      char *p = fgets (buffer[i], sizeof(buffer[0]), fp);
      if (!p)
	break;
      printf ("%s", buffer[i]);
    }

  fclose (fp);

}

void
ParseOpt (int argc, char *argv[])
{
  int opt;
  int bErrorOpt = 0;
  while ((opt = getopt (argc, argv, "hvcdnms")) != -1)
    {
      switch (opt)
      {
	case 'h':
	  bErrorOpt = 1;
	  break;
	case 'v':
	  bVerbose = 1;
	  break;
	case 'c':
	  bCheckCachedPages = 1;
	  break;
	case 'd':
	  bWillNotNeed = 1;
	  break;
	case 'n':
	  bWillNeed = 1;
	  break;
	case 'm':
	  bShowMemInfo = 1;
	  break;
	case 's':
	  bProcessSubDir = 1;
	  break;
      }
    }

  bErrorOpt += CheckOpt (argv);

  if (bErrorOpt)
    exit (-1);
}

int
main (int argc, char *argv[])
{

  clock_t start = clock ();

  ParseOpt (argc, argv);

  if (argc == 1 || optind >= argc)
    AppendToDoList (".");
  else
    {
      OnScreen ("optopt[%d] [%s]\n", optind, argv[optind]);
      int fd = open (argv[optind], O_RDONLY);
      if (FAIL == fd)
	{
	  ShowErrorInfo (__FILE__, __LINE__);
	  return FAIL;
	}

      struct stat statbuf;
      fstat (fd, &statbuf);
      close (fd);
      if (S_ISDIR(statbuf.st_mode))
	{
	  AppendToDoList (argv[optind]);
	  printf ("Start processing [%s]...\n", ToDoList[0]);
	}
      else if (S_ISREG(statbuf.st_mode))
	{
	  OnScreen ("%s\t", argv[optind]);
	  printf ("Analyzing file [%s]...\n", argv[optind]);
	  if (DealOneFile (argv[optind]) == FAIL)
	    iFileFailed++;
	}
      else
	{
	  printf ("%s - Skip\n", argv[optind]);
	}

    }
  const char *pDir;

  while ((pDir = GetHead ()))
    {
      if (ProcessSingleDir (pDir) == FAIL)
	OnScreen ("Skip\n");

    }
  printf ("\nDone! %f Second used.\n",
	  ((double) (clock () - start)) / CLOCKS_PER_SEC);

  printf ("\n");
  ShowStaticInfo ();

  if (bWillNeed || bWillNotNeed)
    printf ("\nCached Adviced !\n");

  if (bShowMemInfo)
    ShowMemInfo ();
  printf ("\n");

  return 0;
}
