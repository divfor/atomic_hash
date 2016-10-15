测试程序：
进入test目录, 确保链接文件有（ln -s ../src/atomic_hash.h ./）, 执行run即可：
     1. 它先编译src并安装动态链接库/usr/lib[64]/libatomtic_hash.so
     2. 它然后编译test并执行之：mthread_test ./top-1m.csv (附带解压的Alexa前100万主机名样本）
     3. 输出结果：屏幕周期性打印的运行情况。

这个测试程序自动检测cpu的个数并取全部核心去运行（超线程不算入），可以加个数字n做第二个参数指定只读取文件前n行
