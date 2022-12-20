#!/bin/bash
# usage: ./auto_test.sh <m1> <m2>

server_8002=5.15.0-48-generic
server_8003=5.4.0-135-generic
HuaWei=?

<<EOF
  isPrimer: Determine whether it is a prime number
  Usage: isPrimer <m>
  Tips: The return value of shell function can only be an integer between 0 and 255, of which only 0 means success, and other values mean failure.
EOF
function isPrimer() {
  # except 2
  if [ $1 -eq 2 ] 
  then
    echo "Number 2 is not satisfied!"
    return 255
  else
    i=2
    while [ $i -le $1 ] 
    do
      mod=`expr $1 % $i`
      if [ $mod == 0 -a $i != $1 ]
      then
        # num isn't a primer
        return 255
      else
        let i++
      fi
    done
    # num is a primer
    return 0
  fi
}

<<EOF
  isPrimer: Returns the smallest one in three numbers
  Usage: min <num1> <num2> <num3>
  Tip: Ternary_Operator in shell --- command1 && { command11; command12; } || { command21; command22; }
  <returnValue> stands for <minValue>
EOF
function min() {
  if [ $# == 2 ]
  then
    [ $1 -lt $2 ] && return $1 || return $2
  elif [ $# == 3 ]
  then
    [ $1 -lt $2 ] && { [ $1 -lt $3 ] && return $1 || return $3; } || { [ $2 -lt $3 ] && return $2 || return $3; }
  elif [ $# -gt 3 ]
  then
    min=65535
    for var in $@; do
      [ $var -lt $min ] && min=$var
    done
    return $min
  else
    echo "Error args!"
    exit 1
  fi
}

# Generate data by argument_list
# limit args: [2, 3]
if [ $# -gt 3 -o $# -lt 2 ]
then
  echo "Usage: ./test.sh <m1> <m2>"
  echo "Usage: ./test.sh <m1> <m2> <m3>"
  exit 1
else
  echo ">> Determine whether the parameter list is all prime numbers..."
  # call isPrimer()
  for var in $@
  do
    isPrimer $var
    # integer 0: success
    # other integer[1, 255]: failure
    if [ $? != 0 ]
    then
      echo "Args must be primer!"
      exit 1
    fi
  done

  echo ">> Executing gendata..."
  filebytes=0
  filelist=()
  primer_number=$#

  # initialize "filelist" array
  index=1
  while [ $index -le $primer_number ]
  do
    filelist[$index]="_testfile${index}"
    let index++
    # echo ${filelist[$index]}
  done
  # judge server version
  case $(uname -r) in
  $server_8002|$server_8003)
    # echo "server 8002|8003"
    filebytes=7516192768    # 7G
    # generate data by ./gendata.sh
    index=1
    while [ $index -le $primer_number ]
    do
      ./gendata.sh ${filebytes} ${filelist[$index]}
      let index+=1
    done
    ;;
  $huawei)
    # echo "server HuaWei"
    filebytes=3221225472
    tmp_files=("tmp_file1", "tmp_file2", "tmp_file3")
    # generate tmp_data by ./gendata.sh
    index=0
    while [ $index -le ${#tmp_files[@]} ]
    do
      ./gendata.sh ${filebytes} ${tmp_files[$index]}
      let index++
    done
    # assemble tmp_file to filelsit
    for file in ${filelist[@]}; do
      index=0
      while [ $index -le ${#tmp_files[@]} ]
      do
        cat ${tmp_files[$index]} >> $file
        let index++
      done
    done
    # rm -rf
    rm -rf tmp_file*
    ;;
  *)
    echo "No Match Server!"
    exit 1
    ;;
  esac
  echo
fi

# compile evenodd -O3 -lpthread
echo ">> Executing compile..."
# wait for a moment to print message
sleep 3
./compile.sh

# start timer
start_time=$(date +%s)

# write
primer_list=()
idx=1
for m in $@; do
  primer_list[$idx]=$m
  let idx++
done
echo ">> Executing write..."
idx=1
while [ $idx -le $primer_number ]
do
  ./evenodd write ${filelist[$idx]} ${primer_list[$idx]}
  let idx++
done
mkdir compare/
echo

# min
min $@
m=$?
idx=1
for primer in ${primer_list[@]}; do
  # omit "&&"
  [ $primer -eq $m ] || let idx++
done

echo ">> Executing read & repair..."
# Testing: Phase 1
column=`expr ${m} - 1`
last=`expr ${m} + 1`
# Losing one column in file
while [ ${column} -le ${last} ]
do
  echo "------------- Loss: column ${column} --------------"
  mv disk_${column}/ compare/
  ./evenodd read ${filelist[$idx]} read${filelist[$idx]}
  diff ${filelist[$idx]} read${filelist[$idx]}

  ./evenodd repair 1 ${column}
  diff disk_${column}/$(($idx-1)) compare/disk_${column}/$(($idx-1))

  let column+=1

  wait
  rm -rf compare/*
  rm -rf read${filelist[$idx]}

  echo
done

# Testing: Phase 2
column1=`expr ${m} - 2`
column2=`expr ${m} - 1`
last=`expr ${m} + 1`

# losing two columns in file
while [ ${column2} -le ${last} ]
do
  echo "------------- Loss: column ${column1} ${column2} --------------"
  mv disk_${column1}/ disk_${column2}/ compare/

  ./evenodd read ${filelist[$idx]} read${filelist[$idx]}
  diff ${filelist[$idx]} read${filelist[$idx]}

  ./evenodd repair 2 ${column1} ${column2}
  diff disk_${column1}/$(($idx-1)) compare/disk_${column1}/$(($idx-1))
  diff disk_${column2}/$(($idx-1)) compare/disk_${column2}/$(($idx-1))

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
  rm -rf read${filelist[0]}

  echo
done

# stop timer
end_time=$(date +%s)

echo "Elapsed time: $[$end_time-$start_time] seconds"
echo
echo "
  _____                              __       _ 
 / ____|                            / _|     | |
| (___  _   _  ___ ___ ___  ___ ___| |_ _   _| |
 \___ \| | | |/ __/ __/ _ \/ __/ __|  _| | | | |
 ____) | |_| | (_| (_|  __/\__ \__ \ | | |_| | |
|_____/ \__,_|\___\___\___||___/___/_|  \__,_|_|
"
