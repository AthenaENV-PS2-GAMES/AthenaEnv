/* Host stub of the PS2SDK libmc.h / libmc-common.h parts the memcard module uses. */
#pragma once
#include <tamtypes.h>

#define MC_WAIT   0
#define MC_NOWAIT 1
#define MC_TYPE_XMC 1

typedef struct _sceMcStDateTime {
    u8 Resv2;
    u8 Sec;
    u8 Min;
    u8 Hour;
    u8 Day;
    u8 Month;
    u16 Year;
} sceMcStDateTime;

typedef struct _sceMcTblGetDir {
    sceMcStDateTime _Create;
    sceMcStDateTime _Modify;
    u32 FileSizeByte;
    u16 AttrFile;
    u16 Reserve1;
    u32 Reserve2;
    u32 PdaAplNo;
    unsigned char EntryName[32];
} sceMcTblGetDir __attribute__((aligned(64)));

#define sceMcResSucceed         0
#define sceMcResChangedCard     -1
#define sceMcResNoFormat        -2
#define sceMcResFullDevice      -3
#define sceMcResNoEntry         -4
#define sceMcResDeniedPermit    -5
#define sceMcResNotEmpty        -6
#define sceMcResUpLimitHandle   -7
#define sceMcResFailReplace     -8
#define sceMcResFailResetAuth   -11
#define sceMcResFailDetect      -12
#define sceMcResFailDetect2     -13
#define sceMcResDeniedPS1Permit -51
#define sceMcResFailAuth        -90

#define sceMcFileInfoCreate 0x01
#define sceMcFileInfoModify 0x02
#define sceMcFileInfoAttr   0x04

int mcInit(int type);
int mcGetInfo(int port, int slot, int *type, int *free, int *format);
int mcOpen(int port, int slot, const char *name, int mode);
int mcClose(int fd);
int mcSeek(int fd, int offset, int origin);
int mcRead(int fd, void *buffer, int size);
int mcWrite(int fd, const void *buffer, int size);
int mcFlush(int fd);
int mcMkDir(int port, int slot, const char *name);
int mcGetDir(int port, int slot, const char *name, unsigned mode, int maxent, sceMcTblGetDir *table);
int mcSetFileInfo(int port, int slot, const char *name, const sceMcTblGetDir *info, unsigned flags);
int mcDelete(int port, int slot, const char *name);
int mcFormat(int port, int slot);
int mcUnformat(int port, int slot);
int mcGetEntSpace(int port, int slot, const char *path);
int mcRename(int port, int slot, const char *oldName, const char *newName);
int mcSync(int mode, int *cmd, int *result);
