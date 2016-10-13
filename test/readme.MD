测试程序以前在centos下做的，在ubuntu 16.10 （4.8内核）下如下编译：
1. clone代码下来，进入src 进行make得到动态链接库libatomic_hash.so，然后make install安装这个库到系统库目录；
     -- 如果之后测试程序make的链接出错，需要把atomic_hash.c里面把几个出错的inline函数改成static或者去掉inline来make
     -- make install在ubuntu下找不到/usr/lib64, 把makefile的/usr/lib64改成/usr/lib吧
2. 进入test目录, ln -s ../src/atomic_hash.h ./, ln -s ../src/hash_city.h ./, make得到mthread_test可执行程序；
3. 找到一些字符串样本，比如我用Alexa top 1M sites，100万主机名：http://s3.amazonaws.com/alexa-static/top-1m.csv.zip， Linux下用unzip解压之，然后./mthread_test top-1m.csv <num_to_read>. 就可以看到屏幕周期性打印的运行情况。第二个参数不写就是读取全部行

这个测试程序自动检测cpu的个数并取非超线程的核心去运行

使用接口因为没有应用到工程，还没仔细定义，你可以自己改动
