#include "object.h"
#include "vm.h"
#include "runtime.h"
#include <errno.h>

extern ObjClass *lxErrClass;
extern ObjClass *lxSystemErrClass;

ObjClass *superClass;
ObjClass *underClass;

Table mappingTbl;

ObjClass *sysErrClass(int err) {
  Value val;
  if (tableGet(&mappingTbl, NUMBER_VAL(err), &val)) {
    return AS_CLASS(val);
  } else {
    return lxSystemErrClass;
  }
}

static void registerError(const char *name, int value) {
  ObjClass *klass = createClass(name, superClass);
  addConstantUnder(name, OBJ_VAL(klass), OBJ_VAL(underClass));
  propertySet(TO_INSTANCE(klass), INTERN("errno"), NUMBER_VAL(value));
  tableSet(&mappingTbl, NUMBER_VAL(value), OBJ_VAL(klass));
}

void Init_ErrorClasses(void) {
  superClass = lxSystemErrClass;
  underClass = lxSystemErrClass;

  initTable(&mappingTbl);

  registerError("E2BIG", E2BIG);
  registerError("EACCES", EACCES);
  registerError("EADDRINUSE", EADDRINUSE);
  registerError("EAGAIN", EAGAIN);
  registerError("EBADF", EBADF);
#ifdef EBADFD
  registerError("EBADFD", EBADFD);
#endif
#ifdef ECONNRESET
  registerError("ECONNRESET", ECONNRESET);
#endif
  registerError("EEXIST", EEXIST);
  registerError("EINTR", EINTR);
  registerError("EINVAL", EINVAL);
  registerError("ENOENT", ENOENT);
  registerError("EPERM", EPERM);
}
