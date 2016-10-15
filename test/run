#!/bin/sh

target=${1:-'./top-1m.csv'}

cd ../test/ || exit
[ -f "${target}" ] || tar zxf ${target}.tar.gz
(cd ../src; make clean; make uninstall;  make; make install)
mkfile="./makefile"
exe=$(awk '/^EXECUTABLE/{print $NF}' ./makefile);

make clean
make

./${exe} ${target}

################################################
exit
curr=$(pwd);
cd $rundir;
rm -rf gmon.out;
exit
read a;
${rundir}/${exe} $target;
if grep ^LINKFLAGS ${curr}/${mkfile} | grep -qs '\-pg'; then
  gprof -b ${rundir}/${exe} gmon.out | less
fi;
cd $curr

