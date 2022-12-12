#!/bin/bash
# usage: ./auto_test.sh <m1> <m2>
server_8002=5.15.0-48-generic
server_8003=5.4.0-135-generic
if [ $(uname -r) == ${server_8002} ]
then
  echo "server 8002"
  # initialize
  filebytes=7516192768    # 7G
  filenames=("_testfile1" "_testfile2")  # fixed
  index=0
  
  # generate data by ./gendata.sh
  while [ $index -lt ${#filenames[@]} ]
  do
    ./gendata.sh ${filebytes} ${filenames[$index]}
    let index+=1
  done
elif [ $(uname -r) == ${server_8003} ]
then
  echo "server 8003"
  # initialize
  filebytes=3221225472    # 3G
  filenames=("_testfile1" "_testfile2" "_testfile3" "_testfile4")
  index=0

  # generate data by ./gendata.sh
  while [ $index -lt ${#filenames[@]} ]
  do
    ./gendata.sh ${filebytes} ${filenames[$index]}
    let index+=1
  done

  # assemble
  cat ${filenames[2]} >> ${filenames[0]}
  cat ${filenames[1]} >> ${filenames[0]}
  cat ${filenames[1]} >> ${filenames[1]}
  cat ${filenames[2]} >> ${filenames[1]}

  rm -rf ${filenames[2]} ${filenames[3]}
else
  echo "unknown server..."
  exit
fi

# compile evenodd
./compile.sh

# write
m1=$1
m2=$2
./evenodd write ${filenames[0]} ${m1} && ./evenodd write ${filenames[1]} ${m2}
mkdir compare/


# test1
column=`expr ${m2} - 1`
last=`expr ${m2} + 1`

# losing one column in file
while [ ${column} -le ${last} ]
do
  echo "------------- Loss: column ${column} --------------"
  mv disk_${column}/ compare/
  ./evenodd read ${filenames[1]} read${filenames[1]}
  diff ${filenames[1]} read${filenames[1]}

  ./evenodd repair 1 ${column}
  diff disk_${column}/1 compare/disk_${column}/1

  let column+=1

  wait
  rm -rf compare/*
  rm -rf read${filenames[1]}
done

# test2
column1=`expr ${m1} - 2`
column2=`expr ${m1} - 1`
last=`expr ${m1} + 1`

# losing two columns in file
while [ ${column2} -le ${last} ]
do
  echo "------------- Loss: column ${column1} ${column2} --------------"
  mv disk_${column1}/ disk_${column2}/ compare/

  ./evenodd read ${filenames[0]} read${filenames[0]}
  diff ${filenames[0]} read${filenames[0]}

  ./evenodd repair 2 ${column1} ${column2}
  diff disk_${column1}/0 compare/disk_${column1}/0
  diff disk_${column1}/1 compare/disk_${column1}/1
  diff disk_${column2}/0 compare/disk_${column2}/0
  diff disk_${column2}/1 compare/disk_${column2}/1

  if [ $column2 != $last ]
  then
    let column1+=1
    let column2+=1
  elif [ $column2 == $last -a `expr $column2 - $column1` == 1 ]
  then
    let column1-=1
  else
    let column2+=1
  fi

  wait
  rm -rf compare/*
  rm -rf read${filenames[0]}
done
