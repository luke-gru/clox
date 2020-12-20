#include "object.h"
#include "vm.h"
#include "runtime.h"
#include "socket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

ObjClass *lxSocketClass;
ObjClass *lxAddrInfoClass;

static void markInternalSocket(Obj *obj) {
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
    peer_addr = (struct sockaddr*)&in_peer_addr;
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

static Value lxSocketGetFd(int argCount, Value *args) {
    Value self = args[0];
    LxFile *f = checkSocket(self);
    int sfd = f->fd;
    return NUMBER_VAL(sfd);
}

// TODO: move this function
static ObjString *numberToString(int num) {
  char numbuf[20];
  memset(numbuf, 0, 20);
  snprintf(numbuf, 20, "%d", num);
  ObjString *buf = emptyString();
  pushCString(buf, numbuf, strlen(numbuf));
  return buf;
}

typedef struct LxAddrInfo {
  struct addrinfo *ai;
  int ai_is_freed;
} LxAddrInfo;

static void markInternalAddrInfo(Obj *obj) {
  // do nothing
}

static void freeInternalAddrInfo(Obj *obj) {
    ASSERT(obj->type == OBJ_T_INTERNAL);
    ObjInternal *internal = (ObjInternal*)obj;
    LxAddrInfo *lai = internal->data;
    if (!lai->ai_is_freed) {
      freeaddrinfo(lai->ai);
    }
    lai->ai_is_freed = true;
    FREE(LxAddrInfo, lai);
}

static LxAddrInfo *getAddrInfoHidden(Value addrInfoVal) {
    ObjInstance *addrInfoInst = AS_INSTANCE(addrInfoVal);
    ObjInternal *internal = addrInfoInst->internal;
    LxAddrInfo *lai = internal->data;
    return lai;
}

static Value makeNewAddrInfo(struct addrinfo *ai) {
    Value addrInfoVal = callFunctionValue(OBJ_VAL(lxAddrInfoClass), 0, NULL);
    ObjInstance *addrInfoInst = AS_INSTANCE(addrInfoVal);
    ObjInternal *internalObj = newInternalObject(false, NULL, sizeof(LxAddrInfo), markInternalAddrInfo, freeInternalAddrInfo,
            NEWOBJ_FLAG_NONE);
    hideFromGC((Obj*)internalObj);
    LxAddrInfo *lai = ALLOCATE(LxAddrInfo, 1);
    lai->ai = ai;
    lai->ai_is_freed = false;
    internalObj->data = lai;
    addrInfoInst->internal = internalObj;
    unhideFromGC((Obj*)internalObj);
    return addrInfoVal;
}

static Value addrInfoArrayList(struct addrinfo *res) {
  struct addrinfo *rp = res;
  Value ret = newArray();
  for (rp = res; rp != NULL; rp = rp->ai_next) {
    arrayPush(ret, makeNewAddrInfo(rp));
  }
  return ret;
}

static Value lxAddrInfoStaticGetAddrInfo(int argCount, Value *args) {
  CHECK_ARITY("AddrInfo.getaddrinfo", 3, 5, argCount);
  Value nodeVal = args[1];
  Value serviceVal = args[2]; // port number or service string like "tcp" or "unix"
  CHECK_ARG_IS_A(nodeVal, lxStringClass, 1);
  ObjString *nodeStr = AS_STRING(nodeVal);
  ObjString *serviceStr = NULL;
  int sockType = 0; // unspecified
  int afamily = AF_UNSPEC;
  int flags = 0;
  if (IS_NUMBER(serviceVal)) {
    serviceStr = numberToString(AS_NUMBER(serviceVal));
    flags |= AI_NUMERICSERV;
  } else {
    if (!IS_NIL(serviceVal)) {
      CHECK_ARG_IS_A(serviceVal, lxStringClass, 2);
      serviceStr = AS_STRING(serviceVal);
    }
  }
  if (argCount >= 4) {
    Value sockTypeVal = args[3];
    if (!IS_NIL(sockTypeVal)) {
      CHECK_ARG_BUILTIN_TYPE(sockTypeVal, IS_NUMBER_FUNC, "number", 4);
      sockType = AS_NUMBER(sockTypeVal);
    }
  }
  if (argCount >= 5) {
    Value afamilyVal = args[4];
    CHECK_ARG_BUILTIN_TYPE(afamilyVal, IS_NUMBER_FUNC, "number", 5);
    afamily = AS_NUMBER(afamilyVal);
  }

  struct addrinfo hints;
  struct addrinfo *res;
  hints.ai_socktype = sockType; // ex: Socket::SOCK_STREAM
  hints.ai_family = afamily; // ex: Socket::AF_INET
  hints.ai_flags = flags;
  hints.ai_protocol = 0;

  int st = getaddrinfo(nodeStr->chars,
      serviceStr ? serviceStr->chars : NULL,
      &hints, &res);
  if (st != 0) {
    throwErrorFmt(sysErrClass(errno), "error in getaddrinfo(2): %s", strerror(errno));
  }

  // When given a numbered port, sin_port is not set correctly
  struct addrinfo *addr;
  for (addr = res; addr != NULL; addr = addr->ai_next) {
    if (addr->ai_family == AF_INET6) {
      // START WORKAROUND
      struct sockaddr_in6 *sockaddr_v6 = (struct sockaddr_in6 *)addr->ai_addr;
      if (sockaddr_v6->sin6_port == 0) {
        sockaddr_v6->sin6_port = htons(AS_NUMBER(serviceVal));
      }
      // END WORKAROUND
    } else if (addr->ai_family == AF_INET) {
      // START WORKAROUND
      struct sockaddr_in *sockadr = (struct sockaddr_in *)addr->ai_addr;
      if (sockadr->sin_port == 0) {
        sockadr->sin_port = htons(AS_NUMBER(serviceVal));
      }
      // END WORKAROUND
    }
  }
  return addrInfoArrayList(res);
}

static Value lxAddrInfoIp(int argCount, Value *args) {
  CHECK_ARITY("AddrInfo#ip", 1, 1, argCount);
  Value self = args[0];
  LxAddrInfo *lai = getAddrInfoHidden(self);
  int afamily = lai->ai->ai_family;
  if (afamily != AF_INET && afamily != AF_INET6) {
    return NIL_VAL;
  }
  ObjString *buf = emptyString();
  if (afamily == AF_INET) {
    struct sockaddr_in *addr_in = (struct sockaddr_in*)lai->ai->ai_addr;
    char *s = inet_ntoa(addr_in->sin_addr); // XXX: not thread-safe
    pushCString(buf, s, strlen(s));
  } else {
    // TODO
  }
  return OBJ_VAL(buf);
}

static Value lxAddrInfoInspect(int argCount, Value *args) {
  CHECK_ARITY("AddrInfo#inspect", 1, 1, argCount);
  Value self = args[0];
  LxAddrInfo *lai = getAddrInfoHidden(self);
  int afamily = lai->ai->ai_family;
  int socktype = lai->ai->ai_socktype;
  ObjString *buf = emptyString();
  pushCStringFmt(buf, "%s", "#<AddrInfo ");

  if (afamily == AF_INET) {
    struct sockaddr_in *addr_in = (struct sockaddr_in*)lai->ai->ai_addr;
    char *s = inet_ntoa(addr_in->sin_addr); // XXX: not thread-safe
    pushCString(buf, s, strlen(s));
    int port = ntohs(addr_in->sin_port);
    pushCStringFmt(buf, ":%d", port);
  } else if (afamily == AF_UNIX) {
    // TODO
  } else if (afamily == AF_INET6) {
    // TODO
  }
  char *canonname = lai->ai->ai_canonname;
  if (canonname && strlen(canonname) > 0) {
    pushCStringFmt(buf, " (%s)", canonname);
  }
  if (socktype == SOCK_STREAM) {
    pushCString(buf, " (TCP)", 6);
  } else if (socktype == SOCK_DGRAM) {
    pushCString(buf, " (UDP)", 6);
  }
  pushCString(buf, ">", 1);
  return OBJ_VAL(buf);
}

void Init_SocketClass(void) {
    lxSocketClass = addGlobalClass("Socket", lxIOClass);
    Value sockVal = OBJ_VAL(lxSocketClass);

    lxAddrInfoClass = addGlobalClass("AddrInfo", lxObjClass);
    ObjClass *addrInfoStatic = classSingletonClass(lxAddrInfoClass);
    addNativeMethod(addrInfoStatic, "getaddrinfo", lxAddrInfoStaticGetAddrInfo);
    /*addNativeMethod(lxAddrInfoClass, "protocol", lxAddrInfoProtocol);*/
    /*addNativeMethod(lxAddrInfoClass, "socktype", lxAddrInfoSocktype);*/
    /*addNativeMethod(lxAddrInfoClass, "afamily", lxAddrInfoSocktype);*/
    addNativeMethod(lxAddrInfoClass, "ip", lxAddrInfoIp);
    addNativeMethod(lxAddrInfoClass, "inspect", lxAddrInfoInspect);

    addNativeMethod(lxSocketClass, "init", lxSocketInit);
    addNativeMethod(lxSocketClass, "connect", lxSocketConnect);
    addNativeMethod(lxSocketClass, "send", lxSocketSend);
    addNativeMethod(lxSocketClass, "bind", lxSocketBind);
    addNativeMethod(lxSocketClass, "accept", lxSocketAccept);
    addNativeGetter(lxSocketClass, "fd", lxSocketGetFd);

    addConstantUnder("AF_UNIX", NUMBER_VAL(AF_UNIX), sockVal);
    addConstantUnder("AF_LOCAL", NUMBER_VAL(AF_LOCAL), sockVal);
    addConstantUnder("AF_INET", NUMBER_VAL(AF_INET), sockVal);

    addConstantUnder("SOCK_STREAM", NUMBER_VAL(SOCK_STREAM), sockVal);
    addConstantUnder("SOCK_DGRAM", NUMBER_VAL(SOCK_DGRAM), sockVal);
}
