/*
 * winfsp_compat.h — WinFsp adaptation shim for the LTFS Windows build.
 *
 * Include this INSTEAD OF <fuse.h> in FUSE-facing translation units
 * (src/ltfs_fuse.c, src/main.c, the kmi/iosched/tape-backend plugins that use
 * fuse_opt, and src/tape_drivers/windows/ltotape/ltotape_platform.c in place
 * of <FUSE4Win_nonstd.h>).
 *
 * Compile with: -I<winfsp>/inc/fuse  (and link winfsp-x64.dll import lib)
 * Status: starting artifact for PORTING_PLAN.md step 1 — not yet compiled
 * against the tree; expect to iterate during build bring-up.
 */
#ifndef LTFS_WINFSP_COMPAT_H
#define LTFS_WINFSP_COMPAT_H

#include <fuse.h> /* WinFsp FUSE 2.8-compatible API (winfsp/inc/fuse) */

/*
 * FUSE4Win proprietary extension stubs (formerly FUSE4Win_nonstd.h).
 *
 * Under FUSE4Win/UMFSDK the kernel driver mounted on the tape device object,
 * so a mounted filesystem instance lost direct device access and had to
 * tunnel SCSI through the FUSE layer. A WinFsp mount is an independent
 * virtual volume: the LTFS process keeps its own \\.\TAPEn handle (opened in
 * ltotape_open with FSCTL_LOCK_VOLUME for exclusivity), so the direct
 * DeviceIoControl path is always the right one.
 *
 * fuse_get_media_device() returning NULL steers every existing call site
 * (ltotape_platform.c:368, and the fd==NULL branch at :199) onto that path.
 */
static inline void *fuse_get_media_device(void)
{
	return NULL;
}

static inline int fuse_media_ioctl(unsigned long ctlcode,
	void *inbuf, unsigned long inlen, void *outbuf, unsigned long outlen)
{
	(void)ctlcode; (void)inbuf; (void)inlen; (void)outbuf; (void)outlen;
	/* Unreachable: ltotape_open always opens a real device handle, so the
	 * fd==NULL branch that calls this can never be taken. Fail loudly if
	 * something does reach it (0 == FALSE for the BOOL-style caller). */
	return 0;
}

/*
 * Gap G3 (PORTING_PLAN.md): WinFsp's fuse_timespec has int64 tv_nsec on x64;
 * the MinGW/LTFS timespec has 32-bit tv_nsec. Field-by-field conversion only —
 * the layouts are NOT compatible for memcpy/cast.
 */
static inline struct fuse_timespec
fuse_timespec_from_timespec_fields(long long sec, long nsec)
{
	struct fuse_timespec ts;
	ts.tv_sec = sec;
	ts.tv_nsec = nsec;
	return ts;
}

/*
 * Gap G5 sanity: the FUSE callback signatures exchange 64-bit offsets.
 * win_util.h already forces _FILE_OFFSET_BITS=64; make sure it held.
 */
#ifdef __cplusplus
static_assert(sizeof(fuse_off_t) == 8, "fuse_off_t must be 64-bit");
#else
typedef char ltfs_winfsp_off_t_must_be_64bit[(sizeof(fuse_off_t) == 8) ? 1 : -1];
#endif

#endif /* LTFS_WINFSP_COMPAT_H */
