#!/usr/bin/env zsh


function insert () {
  # SIZE PREF_LENGTH DENS INSTANCE_NUMBER CPLEX_TIME CPLEX_SIZE
  # ROUNDINGSAT_TIME ROUNDINGSAT_SIZE
  sqlite3 --batch timing.sqlite3 "insert into timing values('$1', '$2', '$3', '$4', '$5', '$6', '$7', '$8');"
}


function do_test () {
  # SIZE PREF_LENGTH DENS INSTANCE_NUMBER
  RESULTS=$(./pbo $1 $2 $3 testing $4 | awk '/took/ {time = $3} /found/ {size = $3} END {print time,size}')
  cplex_arr=(${(s/ /)RESULTS})
  RESULTS=$(../../../roundingsat/roundingsat-O3 --verbosity=0 testing-v2.pbo | awk '/CPU time/ {time = $5} /objective function value/ {size = $5} END {print time,size}')
  sat_arr=(${(s/ /)RESULTS})
  (( sat_size = $1 - $sat_arr[2] / 2 ))
  insert $1 $2 $3 $4 $cplex_arr[1] $cplex_arr[2] $sat_arr[1] $sat_size
}


if [ ! -e "timing.sqlite3" ] ; then
  sqlite3 timing.sqlite3 'create table timing(size INTEGER, pref_length INTEGER, dens REAL, number INTEGER, cplex_time REAL, cplex_size INTEGER, sat_time REAL, sat_size INTEGER);';
fi

for size in {50..1000..50} ; do
  echo "On size ${size}"
  for pref_length in 3 5 10 25 ; do
    for dens in 0.75 0.85 0.95 ; do
      for iteration in {0..50} ; do
        do_test $size $pref_length $dens $iteration
      done
    done
  done
done
