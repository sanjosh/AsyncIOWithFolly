#include "AsyncIO.h"

#include <folly/futures/Future.h>
#include <folly/futures/Unit.h>
#include <folly/Baton.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
//#include <poll.h>
#include <sys/epoll.h> // epoll_ctl
#include <sys/eventfd.h> // EFD_NONBLOCK

#include <cstdlib>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <future>

#include <glog/logging.h>

//#include <folly/experimental/io/FsUtil.h>
//#include <folly/ScopeGuard.h>
//#include <folly/String.h>

//namespace fs = folly::fs;
using folly::AsyncIO;
using folly::AsyncIOQueue;
using folly::AsyncIOOp;

#define myoffsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define mycontainer_of(ptr, type, member) ({                      \
      const decltype(((type *)0)->member) * __mptr = (ptr);     \
            (type *)((char *)__mptr - myoffsetof(type, member)); })

struct FilerJob
{
  AsyncIOOp opera;
  folly::Promise <ssize_t> p;
};

class IOExecutor
{
  public:
  IOExecutor(size_t queueDepth)
    : ioexec(queueDepth, AsyncIO::PollMode::POLLABLE)
    , ioqueue(&ioexec)
  {
    epollFD_ = epoll_create1(0);
    assert(epollFD_ >= 0);

    {
      epoll_event epollEvent;
      int asyncFD = ioexec.pollFd();
      assert(asyncFD != -1);
      bzero(&epollEvent, sizeof(epollEvent));
      epollEvent.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
      int retcode = epoll_ctl(epollFD_, EPOLL_CTL_ADD, asyncFD, &epollEvent);
      assert(retcode >= 0);
    }

    {
      epoll_event epollEvent;
      shutdownFD_ = eventfd(0, EFD_NONBLOCK);
      assert(shutdownFD_ >= 0);
      bzero(&epollEvent, sizeof(epollEvent));
      epollEvent.data.ptr = (void*)this;
      epollEvent.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
      int retcode = epoll_ctl(epollFD_, EPOLL_CTL_ADD, shutdownFD_, &epollEvent);
      assert(retcode >= 0);
    }

    callbacker_ = std::thread(std::bind(&IOExecutor::callbackFn, this));
  }

  static void cb(AsyncIOOp* op) 
  {
    FilerJob* j = mycontainer_of(op, FilerJob, opera);
    j->p.setValue(op->result());
  }

  folly::Future<ssize_t> submit(FilerJob* j)
  {
    j->opera.setNotificationCallback(&IOExecutor::cb);
    ioqueue.submit(&j->opera);
    return j->p.getFuture();
  }

  void callbackFn()
  {
    ioexec.initializeContext(); 
    // normally context initialized when first req is submitted
    // we want to do it now otherwise core dump
    std::cout << "started callback thread" << std::endl;
    bool terminated = false;
    while (!terminated)
    {
      epoll_event events[2];
      bzero(events, sizeof(epoll_event));

      int numEvents = 0;
      do {
        numEvents = epoll_wait(epollFD_, events, 2, -1);
      } while (numEvents < 0 && errno == EINTR);

      //std::cout << "got event=" << numEvents << std::endl;
      for (int i = 0; i < numEvents; i++) {
        if (events[i].data.ptr != this) {
          auto op = ioexec.pollCompleted();
          //std::cout << "finished=" << op.size() << std::endl;
        } else {
          int64_t garbage;
          ssize_t ret = read(shutdownFD_, &garbage, sizeof(garbage));
          assert(ret == sizeof(garbage));
          terminated = true;
        }
      }
    }
    endCallback.post();
    std::cout << "exiting callback thread" << std::endl;
  }

  ~IOExecutor()
  {
    int64_t garbage; // must write an int
    ssize_t ret = write(shutdownFD_, &garbage, sizeof(garbage));
    int capture_errno = errno;
    assert(ret == sizeof(garbage));
    std::cout << "asking callback thread to exit" << std::endl;

    endCallback.wait();
    callbacker_.join();

    wait();
  }

  void wait() 
  {
    while (ioexec.pending()) {
      auto op = ioexec.pollCompleted();
      //std::cout << "end of =" << op.size() << std::endl;
    }
  }

  AsyncIO ioexec;
  AsyncIOQueue ioqueue;

  int epollFD_ = -1;
  int shutdownFD_ = -1;

  folly::Baton<> endCallback;

  std::thread callbacker_;

};

std::unique_ptr<IOExecutor> io;

int threadFunc(int param)
{
  int count = 102400;
  std::vector<FilerJob> jvec(count);
  std::string fileName = "/mnt/abc" + std::to_string(param);

  int fd = open(fileName.c_str(), 
    O_DIRECT | O_RDWR | O_CREAT | O_TRUNC,
    S_IRWXU | S_IRWXG | S_IRWXO);

  char* buf = (char*) malloc (4096);

  for (int i = 0; i < count; i++)
  {
    memset(buf, 'a' + (i%26), 4096);
    jvec[i].opera.pwrite(fd, buf, 4096, i * 4096);
    auto op = &jvec[i];
    io->submit(op).then([&count, op] () {
      count --;
    });
  }

  int i = 0;
  for (auto& elem : jvec)
  {
    while (elem.opera.state() == AsyncIOOp::State::PENDING)
    {
      std::cout << "thread=" << param << " waiting for i=" << i << std::endl;
      io->wait(); 
    }
    i++;
  }
  close(fd);
  std::cout << "thread=" << param << " done" << std::endl;
  free(buf);

  return 0;
}

template<typename T, typename ...Args>
std::unique_ptr<T> 
mymake_unique( Args&& ...args )
{
  return std::unique_ptr<T>( new T( std::forward<Args>(args)... ) );
} 


int main(int argc, char* argv[])
{
  int numThr = 1;
  if (argc > 1) numThr = atoi(argv[1]);

  io = mymake_unique<IOExecutor>(1024);

  std::vector<std::future<int>> threadVec;

  for (int i = 0; i < numThr; i++)
  {
    auto f = std::async(std::launch::async, 
      threadFunc, 
      i);

    threadVec.emplace_back(std::move(f));
  }

  for (auto& elem : threadVec)
  {
    elem.wait();
  }
}
