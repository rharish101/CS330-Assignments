init_disk() {
	mbs=0
	verbose=0
	obj_file="./objfs"
	for arg in "$@"; do
		if [[ "$arg" =~ ^[0-9]+$ ]]; then
			mbs=$arg
			echo "mbs - $arg"
		elif [ "$arg" == "-v" ]; then
			verbose=1
		elif [ "$arg" == "--cached" ]; then
			obj_file="./objfs_cached"
		fi
	done
	dir=`pwd`
	cd ..

	fusermount -uq mnt
	rm -rf mnt
	rm -f disk.img
	mkdir -p mnt
	if [ $verbose -eq 1 ]; then
		dd if=/dev/zero of=disk.img bs=1M count=$mbs
		$obj_file mnt -o use_ino
	else
		dd if=/dev/zero of=disk.img bs=1M count=$mbs 2> /dev/null
		$obj_file mnt -o use_ino > /dev/null
	fi
	ret=0
	mountpoint -q mnt
	if [ $? -ne 0 ]; then
		echo "$0: mount failed"
		ret=1
	fi
	cd $dir
	return $ret
}

deinit_disk() {
	dir=`pwd`
	cd ..
	fusermount -uq mnt
	rm -f disk.img
	cd $dir
}

if [ "$1" == "-d" ]; then
	deinit_disk
else
	init_disk $*
	exit $?
fi
