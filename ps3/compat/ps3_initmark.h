/*
 * NBlood-PS3: which file's C++ static initializers are running, in
 * USRDIR/nblood-ps3.start.log (with USRDIR/logs.txt). Forced into every
 * C++ file (ps3/Makefile): this initializer comes first in its file's, so
 * the start log's last line names the file whose initializers died.
 */
#ifdef __cplusplus
extern "C" int PS3_InitMark(const char *file);
static int ps3_initmark_ __attribute__((unused)) = PS3_InitMark(__BASE_FILE__);   /* the .cpp, not this header */
#endif
