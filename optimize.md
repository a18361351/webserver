# 如何去检查服务器的性能瓶颈？
## pidstat命令
pidstat命令可以查看一个进程的具体情况
```sh
# pidstat命令可以分析一个正在运行的进程的详细信息
# -t 表示显示进程的线程情况
# -u CPU分析
#   %usr   ：用户空间使用的cpu%
#   %system：内核空间使用的cpu%
#   %guest ：管理程序(hypervisor)为另一个虚拟进程提供服务而等待的虚拟CPU占比
# -w 上下文切换 
#   cswch/s  ：平均每秒钟发生的上下文切换次数。
#   nvcswch/s：平均每秒钟发生的进程放弃 CPU 控制权的次数（进程主动放弃 CPU 控制权所引起的上下文切换）
# -d 磁盘分析
#   kB_rd/s  ：磁盘每秒读数据（单位：KB）
#   kB_wr/s  ：磁盘每秒写数据（单位：KB）
#   kB_ccwr/s：已被任务取消写入磁盘的数据（单位：KB）
# -p PID
pidstat -t -u -p pid 1  # 线程情况，CPU分析，进程PID=pid，每隔1秒刷新
pidstat -t -w -p pid 1  # 分析的是上下文切换
```

## strace命令
```sh
sudo strace -c -f -p pid    # count, fork, PID=pid
```

## perf命令
```sh
sudo perf record -F freq -ag -p pid # -a=--all-cpus, -g=Enable call-graph recording, -F=--freq
```