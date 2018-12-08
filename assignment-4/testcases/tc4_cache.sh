if [ ! -f ../objfs_cached ]; then
	>&2 echo "$0: Cached fs missing"
	exit 0
fi

diff ../objfs ../objfs_cached
if [ $? -eq 0 ]; then
	>&2 echo "$0: Cached and uncached fs are same"
	exit 0
fi

create_files() {
	max=$1
	i=1
	while [[ $i -le $max ]]; do
		file="tc4_file_$i"
		file1="tmp_files/$file"
		file2="../mnt/$file"
		if [[ ! -e $file1 ]]; then
			dd if=/dev/urandom of=$file1 bs=$2 count=1 2> /dev/null
		fi
		cp -f $file1 $file2
		if [ $? -ne 0 ]; then
			>&2 echo "$file2 creation failed"
			return 0
		fi
		i=$(( i + 1 ))
	done
	return 1
}

sanity_test() {
	max=$2
	i=$1
	while [[ $i -le $max ]]; do
		file="tc4_file_$i"
		file1="tmp_files/$file"
		file2="../mnt/$file"
		diff $file1 $file2
		if [ $? -ne 0 ]; then
			>&2 echo "Bad file $file2"
			return 0
		fi
		i=$(( i + 1 ))
	done
	return 1
}

time_test() {
	max=$2
	i=$1
	while [[ $i -le $max ]]; do
		file="tc4_file_$i"
		file1="tmp_files/$file"
		file2="../mnt/$file"
		filesize=`stat --printf="%s" $file1`
		./read $file $filesize > /dev/null
		i=$(( i + 1 ))
	done
	return 1
}

_time() {
	SECONDS=0
	_st=`date +%s.%3N`
	$*
	_et=`date +%s.%3N`
	#_t=$(( (_et - _st) * 1000 ))
	echo "$_et-$_st" | bc
	#echo "Time taken = $SECONDS $_t"
}

remount() {
	dir=`pwd`
	echo "Remounting ..."
	cd ..
	fusermount -u mnt

	x=`pgrep objfs_cached`
	while [ "$x" != "" ]; do
		sleep 1
		x=`pgrep objfs_cached`
	done
	./objfs_cached mnt -o use_ino
	cd $dir
}

max=100

./init_disk.sh 1024 --cached
create_files $max 100K
echo "$0: $max files created on cached fs"
sanity_test 1 $max
if [ $? -eq 0 ]; then
	>&2 echo "$0: Sanity test failed"
	exit 0
fi

remount
sanity_test 1 $max
if [ $? -eq 0 ]; then
	>&2 echo "$0: Sanity test failed after first remount"
	exit 0
fi

remount
sudo bash -c 'echo 3 > /proc/sys/vm/drop_caches '
echo "$0: First read"
_time time_test 1 $max > "time"

sudo bash -c 'echo 3 > /proc/sys/vm/drop_caches '

echo "$0: Second read"
_time time_test 1 $max >> "time"

remount

sudo bash -c 'echo 3 > /proc/sys/vm/drop_caches '
echo "$0: Third read"
_time time_test 1 $max >> "time"

./init_disk.sh -d
exit 4
