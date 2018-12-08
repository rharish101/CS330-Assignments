marks=0

terminate() {
	>&2 echo "terminated"
	exit
}

rm -f "tc_logs" "tc_error_logs" "marks"
touch "tc_logs" "tc_error_logs" "marks"

./init_disk.sh 1024 -v || terminate
echo "Basic test" | tee "tc_logs" "tc_error_logs" "marks"
./test_basic.sh 2> "tc_error_logs" > "tc_logs"
ret=$?
if [[ $ret -eq 0 ]]; then
	echo "basic test failed" | tee -a "tc_error_logs"
else

	for tc in tc*.sh; do
		echo "$tc" | tee -a "tc_logs" "tc_error_logs"
		"./$tc" 2>> "tc_error_logs" >> "tc_logs"
		tc_marks=$?
		echo "$tc: marks = $tc_marks" | tee -a "marks"
		marks=$(( $marks + $tc_marks ))
	done
fi
./init_disk.sh -d
echo "total marks $marks" | tee -a "marks"
echo "Scaled marks" | tee -a "marks"
echo "$marks/2" | bc -l | tee -a "marks"

exit $marks
