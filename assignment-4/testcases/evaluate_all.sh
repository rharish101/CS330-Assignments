for folder in ../submissions/*; do
	printf "\n\n\n\n"
	echo $(basename -- "$folder")
	while true; do
		file="$folder/objstore.c"
		touch "tc_logs" "time"
		if [ ! -f "$file" ]; then
			echo "file not found" | tee "tc_error_logs"
			echo "0" > "marks"
			break
		fi
		cp "$file" ../
		make -C .. clean
		make -C ..
		if [ ! -f "../objfs" ]; then
			echo "Unable to compile" | tee "tc_error_logs"
			echo "0" > "marks"
			break
		fi
		make -C .. objfs_cached
		if [ ! -f "../objfs_cached" ]; then
			echo "Unable to compile cached version" | tee "tc_error_logs"
		fi
		./run_test_cases.sh
		break
	done
	mv "marks" "tc_logs" "tc_error_logs" "time" "$folder""/"
done

