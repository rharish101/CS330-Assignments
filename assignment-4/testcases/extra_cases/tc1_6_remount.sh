BASEFILENAME=$(basename -- "$0")
FILENAME_C="${BASEFILENAME%%.*}_file_c"
FILENAME_D="${BASEFILENAME%%.*}_file_d"
FILENAME_MV="${BASEFILENAME%%.*}_file_mv"
FILENAME_MV2="new_name"
FILECONTENT_C="file1 content to create"
FILECONTENT_D="file1 content to delete"
FILECONTENT_MV="file1 content to rename"

./delete $FILENAME_C		# just try delete before creating
./delete $FILENAME_D		# just try delete before creating
./delete $FILENAME_MV		# just try delete before creating
./delete $FILENAME_MV2		# just try delete before creating

./create $FILENAME_C "$FILECONTENT_C"
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: $FILENAME_C creation failed, returned $ret"
	exit 0
fi

./create $FILENAME_D "$FILECONTENT_D"
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: $FILENAME_D creation failed, returned $ret"
	exit 0
fi

./delete $FILENAME_D
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: deletion failed, returned $ret"
	exit 0
fi

./create $FILENAME_MV "$FILECONTENT_MV"
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: $FILENAME_MV creation failed, returned $ret"
	exit 0
fi

./rename $FILENAME_MV $FILENAME_MV2
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: rename failed, returned $ret"
	exit 0
fi

############################ remount ##################################
dir=`pwd`
echo "Remounting ..."
cd ..
fusermount -u mnt
./objfs mnt -o use_ino
cd $dir
############################ remount ##################################

stat ../mnt/$FILENAME_C
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: created file missing; stat returned $ret"
	exit 0
fi

read_content=$(./read $FILENAME_C ${#FILECONTENT_C})
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: reading $FILENAME_C failed, returned $ret"
	exit 0
fi

if [ "$FILECONTENT_C" != "$read_content" ]; then
	echo "$0: $FILENAME_C match failed"
	exit 0
fi

stat ../mnt/$FILENAME_D
ret=$?
if [ $ret -ne 1 ]; then
	echo "$0: delete actually failed - old file found; stat returned $ret"
	exit 0
fi

stat ../mnt/$FILENAME_MV
ret=$?
if [ $ret -ne 1 ]; then
	echo "$0: rename actually failed - old file found; stat returned $ret"
	exit 0
fi

stat ../mnt/$FILENAME_MV2
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: rename actually failed - new file missing; stat returned $ret"
	exit 0
fi

read_content=$(./read $FILENAME_MV2 ${#FILECONTENT_MV})
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: reading $FILENAME_MV2 failed, returned $ret"
	exit 0
fi

if [ "$FILECONTENT_MV" != "$read_content" ]; then
	echo "$0: $FILENAME_MV match failed"
	exit 0
fi
exit 1
