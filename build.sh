sources=$(find src -name "*.c")
headers=$(find src -name "*.h")
langs=$(find lang -mindepth 1 -maxdepth 1 -type d)
objects=
do_linking=false
program=
do_debug=false

common_flags="-g"
compiler_flags="$common_flags -Wall -Wextra -Ibuild -Isrc"
linker_flags="$common_flags"
linker_libs="-lmagic -lcurses"

set -o xtrace

mkdir -p build/tests || exit

for h in $headers
do
	if [ src/purec.h -nt build/purec.h.gch ] ||
		[ $h -nt build/purec.h.gch ]
	then
		gcc src/purec.h -o build/purec.h.gch || exit
		break
	fi
done

for s in $sources
do
	o="build/${s:4:-2}.o"
	h="${s::-2}.h"
	objects="$objects $o"
	if [ $s -nt $o ] || [ $h -nt $o ] || [ build/purec.h.gch -nt $s ]
	then
		gcc $compiler_flags -c $s -o $o || exit
		do_linking=true
	fi
done

if $do_linking || [ ! -f build/purec ]
then
	gcc $linker_flags $objects -o build/purec $linker_libs || exit
fi

for l in $langs
do
	gcc $compiler_flags -shared -o build/$(basename $l).so -fPIC $l/*
done

while [ ! $# = 0 ]
do
	case $1 in
	-t)
		program=test
		shift
		[ $# = 0 ] && ( echo "-t is missing argument" && exit )
		[ tests/$1.c -nt build/tests/$1.o ] &&
			( gcc $compiler_flags -c tests/$1.c -o build/tests/$1.o || exit )
		exc_objects="${objects/'build/main.o'/} build/tests/$1.o"
		gcc $linker_flags $exc_objects -o build/test $linker_libs || exit
		;;
	-x)
		program=purec
		;;
	-g)
		[ ! -z "$program" ] && program=purec
		do_debug=true
		;;
	esac
	shift
done

if $do_debug
then
	gdb ./build/$program
elif [ ! -z "$program" ]
then
	time_now=$(date "+%s %N")
	read start_seconds start_nanoseconds <<< "$time_now"
	./build/$program
	exit_code=$?
	time_now=$(date "+%s %N")
	read end_seconds end_nanoseconds <<< "$time_now"
	diff_seconds=$((10#$end_seconds - 10#$start_seconds))
	diff_nanoseconds=$((10#$end_nanoseconds - 10#$start_nanoseconds))
	if [ $diff_nanoseconds -lt 0 ]
	then
		diff_seconds=$((diff_seconds - 1))
		diff_nanoseconds=$((1000000000 + diff_nanoseconds))
	fi
	echo -e "exit code: \e[36m$exit_code\e[0m; elapsed time: \e[36m$diff_seconds.$diff_nanoseconds\e[0m seconds"
fi
