#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "socket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

ObjClass *lxSocketClass;

static void markInternalSocket(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    // do nothing
}

static void freeInternalSocket(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxFile *f = (LxFile*)internal->data;
    ASSERT(f->sock);
    FREE(LxSocket, f->sock);
    FREE(LxFile, f);
}

static void initSocketFromFd(Value sockVal, int domain, int type, int proto, int fd) {
    ObjInstance *sockObj = AS_INSTANCE(sockVal);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxFile), markInternalSocket, freeInternalSocket,
            NEWOBJ_FLAG_NONE);
    hideFromGC((Obj*)internalObj);
    LxSocket *sock = ALLOCATE(LxSocket, 1);
    LxFile *file = ALLOCATE(LxFile, 1);
    file->fd = fd;
    file->mode = 0;
    file->oflags = 0;
    file->isOpen = true;
    file->sock = sock;
    file->name = NULL;
    sock->domain = domain;
    sock->type = type;
    sock->proto = 0;
    sock->server = false;
    sock->connected = false;
    sock->proto = proto;
    internalObj->data = file;
    sockObj->internal = internalObj;
    unhideFromGC((Obj*)internalObj);
}

static Value lxSocketInit(int argCount, Value *args) {
    CHECK_ARITY("Socket#init", 3, 4, argCount);
    Value self = args[0];
    CHECK_ARG_BUILTIN_TYPE(args[1], IS_NUMBER_FUNC, "number", 1);
    CHECK_ARG_BUILTIN_TYPE(args[2], IS_NUMBER_FUNC, "number", 2);
    int domain = AS_NUMBER(args[1]);
    int type = AS_NUMBER(args[2]);
    int proto = 0;
    if (argCount == 4) {
      CHECK_ARG_BUILTIN_TYPE(args[3], IS_NUMBER_FUNC, "number", 3);
      proto = AS_NUMBER(args[3]);
    }
    releaseGVL(THREAD_STOPPED);
    int res = socket(domain, type, proto);
    acquireGVL();
    if (res == -1) {
      throwErrorFmt(sysErrClass(errno), "Error creating socket: %s", strerror(errno));
    }
    initSocketFromFd(self, domain, type, proto, res);
    return self;
}

static LxFile *checkSocket(Value sock) {
  LxFile *f = FILE_GETHIDDEN(sock);
  if (f->fd < 0) {
    throwErrorFmt(lxErrClass, "uninitialized socket");
  }
  return f;
}

static Value lxSocketConnect(int argCount, Value *args) {
    CHECK_ARITY("Socket#connect", 2, 3, argCount);
    Value self = args[0];
    Value addr = args[1];
    CHECK_ARG_IS_A(addr, lxStringClass, 1);
    Value port = NIL_VAL;
    if (argCount == 3) {
      port = args[2];
      CHECK_ARG_BUILTIN_TYPE(port, IS_NUMBER_FUNC, "number", 2);
    }
    LxFile *f = checkSocket(self);
    LxSocket *sock = f->sock;
    int res = 0;
    if (sock->domain == AF_INET) {
      struct sockaddr_in in_serv_addr;
      in_serv_addr.sin_family = sock->domain;
      in_serv_addr.sin_port = htons(AS_NUMBER(port));
      // Convert IPv4 and IPv6 addresses from text to binary form
      if (inet_pton(sock->domain, AS_CSTRING(addr), &in_serv_addr.sin_addr) <= 0) {
        throwErrorFmt(lxArgErrClass, "Invalid address to Socket#connect: %s", strerror(errno));
        return -1;
      }
      releaseGVL(THREAD_STOPPED);
      res = connect(f->fd, (struct sockaddr *)&in_serv_addr, sizeof(in_serv_addr));
      acquireGVL();
    } else if (sock->domain == AF_UNIX) {
      /*Value path = args[1];*/
      /*CHECK_ARG_IS_A(path, lxStringClass, 1);*/
      /*my_un_addr.sun_family = AF_UNIX;*/
      /*my_addr = (struct sockaddr*)&my_un_addr;*/
      /*strncpy(my_un_addr.sun_path, AS_CSTRING(path), sizeof(my_un_addr.sun_path)-1);*/
      struct sockaddr_un un_serv_addr;
      un_serv_addr.sun_family = AF_UNIX;
      strncpy(un_serv_addr.sun_path, AS_CSTRING(addr), sizeof(un_serv_addr.sun_path)-1);
      releaseGVL(THREAD_STOPPED);
      res = connect(f->fd, (struct sockaddr *)&un_serv_addr, sizeof(un_serv_addr));
      acquireGVL();
    } else {
      throwErrorFmt(lxErrClass, "Not implemented error");
    }
    if (res < 0) {
      throwErrorFmt(sysErrClass(errno), "Error during connect: %s", strerror(errno));
    }
    sock->connected = true;
    f->isOpen = true;
    return self;
}

static Value lxSocketSend(int argCount, Value *args) {
    // TODO: support flags like MSG_DONTWAIT
    CHECK_ARITY("Socket#send", 2, 2, argCount);
    Value self = args[0];
    Value string = args[1];
    return callMethod(AS_OBJ(self), INTERN("write"), 1, &string, NULL);
}

static Value lxSocketBind(int argCount, Value *args) {
    CHECK_ARITY("Socket#bind", 2, 3, argCount);
    Value self = args[0];
    LxFile *f = checkSocket(self);
    LxSocket *sock = f->sock;
    int fd = f->fd;
    struct sockaddr *my_addr;
    if (sock->domain == AF_UNIX) {
      struct sockaddr_un my_un_addr;
      Value path = args[1];
      CHECK_ARG_IS_A(path, lxStringClass, 1);
      my_un_addr.sun_family = AF_UNIX;
      my_addr = (struct sockaddr*)&my_un_addr;
      strncpy(my_un_addr.sun_path, AS_CSTRING(path), sizeof(my_un_addr.sun_path)-1);
      if (bind(fd, my_addr, sizeof(struct sockaddr_un)) == -1) {
        throwErrorFmt(sysErrClass(errno), "Error during bind: %s", strerror(errno));
      }

      // TODO: make listen a separate call?
      if (listen(fd, 50) == -1) {
        throwErrorFmt(sysErrClass(errno), "Error during listen: %s", strerror(errno));
      }

      return BOOL_VAL(true);
    } else {
      Value addr = args[1];
      int port = 80;
      if (argCount == 3) {
        Value portVal = args[2];
        CHECK_ARG_BUILTIN_TYPE(portVal, IS_NUMBER_FUNC, "number", 2);
        port = AS_NUMBER(portVal);
      }
      struct sockaddr_in my_in_addr;
      my_in_addr.sin_family = AF_INET;
      my_in_addr.sin_addr.s_addr = INADDR_ANY;

      // This ip address will change according to the machine
      my_in_addr.sin_addr.s_addr = inet_addr(AS_CSTRING(addr));

      my_in_addr.sin_port = htons(port);
      my_addr = (struct sockaddr*)&my_in_addr;
      releaseGVL(THREAD_STOPPED);
      if (bind(fd, my_addr, sizeof(struct sockaddr_in)) == -1) {
        acquireGVL();
        throwErrorFmt(sysErrClass(errno), "Error during bind: %s", strerror(errno));
      }

      // TODO: make listen() a separate call
      if (listen(fd, 50) == -1) {
        acquireGVL();
        throwErrorFmt(sysErrClass(errno), "Error during listen: %s", strerror(errno));
      }
      acquireGVL();
    }

    return self;
}

static Value newSocketFromAccept(Value serverSock, int newFd, struct sockaddr *peer_addr, size_t addr_size) {
  // NOTE: call newInstance directly to avoid call to Socket#init, which calls
  // the socket(2) system call
  ObjInstance *newSockObj = newInstance(lxSocketClass, NEWOBJ_FLAG_NONE);
  LxFile *servFile = FILE_GETHIDDEN(serverSock);
  LxSocket *servSock = servFile->sock;
  Value newSock = OBJ_VAL(newSockObj);
  // TODO: save peer information in new socket's servSock
  initSocketFromFd(newSock, servSock->domain, servSock->type, servSock->proto, newFd);
  return newSock;
}

static Value lxSocketAccept(int argCount, Value *args) {
    CHECK_ARITY("Socket#accept", 1, 1, argCount);
    Value self = args[0];
    LxFile *f = checkSocket(self);
    int sfd = f->fd;
    struct sockaddr *peer_addr;
    struct sockaddr_in in_peer_addr; // TODO: support unix
    peer_addr = &in_peer_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);

    releaseGVL(THREAD_STOPPED);
    int newFd = accept(sfd, peer_addr, &addr_size);
    acquireGVL();
    if (newFd == -1) {
      throwErrorFmt(sysErrClass(errno), "Error during accept: %s", strerror(errno));
    }
    Value newSock = newSocketFromAccept(self, newFd, peer_addr, addr_size);
    return newSock;
}

void Init_SocketClass(void) {
    lxSocketClass = addGlobalClass("Socket", lxIOClass);
    Value sockVal = OBJ_VAL(lxSocketClass);

    addNativeMethod(lxSocketClass, "init", lxSocketInit);
    addNativeMethod(lxSocketClass, "connect", lxSocketConnect);
    addNativeMethod(lxSocketClass, "send", lxSocketSend);
    addNativeMethod(lxSocketClass, "bind", lxSocketBind);
    addNativeMethod(lxSocketClass, "accept", lxSocketAccept);

    addConstantUnder("AF_UNIX", NUMBER_VAL(AF_UNIX), sockVal);
    addConstantUnder("AF_LOCAL", NUMBER_VAL(AF_LOCAL), sockVal);
    addConstantUnder("AF_INET", NUMBER_VAL(AF_INET), sockVal);

    addConstantUnder("SOCK_STREAM", NUMBER_VAL(SOCK_STREAM), sockVal);
    addConstantUnder("SOCK_DGRAM", NUMBER_VAL(SOCK_DGRAM), sockVal);
}
