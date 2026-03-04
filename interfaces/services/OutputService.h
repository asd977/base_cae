#ifndef OUTPUTSERVICE_H
#define OUTPUTSERVICE_H

class QString;

struct OutputService {
  // 0-日志，1-进度
  virtual void outputLog(QString str) = 0;
  virtual ~OutputService() {}
};

#endif  // OUTPUTSERVICE_H
