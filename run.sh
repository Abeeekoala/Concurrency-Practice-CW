#!/bin/bash

# --- Configuration ---
# The number of times to run the command
TOTAL_RUNS=500

# The command to benchmark
COMMAND_TO_RUN="./temp/build-release/demo_refinable 8 4 100000"

# --- End of Configuration ---

# Initialize total time to zero
total_time_ms=0
failed_runs=0
all_times=() # Array to store all individual run times

echo "Starting benchmark..."
echo "Command: $COMMAND_TO_RUN"
echo "Runs:    $TOTAL_RUNS"
echo "-----------------------------------------------------"

# Start the loop
for (( i=1; i<=TOTAL_RUNS; i++ )); do
  # Print progress (e.g., "Run 1/500... ")
  # -n means do not output a new line
  echo -n -e "Run $i/$TOTAL_RUNS... "

  # Run the command and capture its output
  output=$(eval $COMMAND_TO_RUN)

  # Extract the time.
  # 1. 'grep " ms"' finds the line with " ms"
  # 2. 'awk '{print $1}'' prints the first word of that line (the number)
  time_ms=$(echo "$output" | grep " ms" | awk '{print $1}')

  # Check if we successfully extracted a number
  if [[ -n "$time_ms" && "$time_ms" =~ ^[0-9]+$ ]]; then
    # Add to the total
    total_time_ms=$((total_time_ms + time_ms))
    all_times+=($time_ms) # Store individual time
    echo "($time_ms ms)"
  else
    # Handle a failed run
    echo "(Failed to extract time from output!)"
    failed_runs=$((failed_runs + 1))
  fi
done

# Calculate successful runs
successful_runs=$((TOTAL_RUNS - failed_runs))

echo "-----------------------------------------------------"
echo "Benchmark Finished"
echo ""
echo "Total Runs:    $TOTAL_RUNS"
echo "Successful:    $successful_runs"
echo "Failed:        $failed_runs"

# Calculate and print the average, variance, and standard deviation
if [ $successful_runs -gt 0 ]; then
  # Use 'bc' (basic calculator) for floating-point math
  # We use a higher scale for intermediate calculations to maintain precision
  average_time=$(echo "scale=4; $total_time_ms / $successful_runs" | bc)
  
  echo "Total Time:    ${total_time_ms} ms"
  echo "Average Time:  ${average_time} ms"

  # --- Calculate Variance ---
  sum_of_squared_diffs="0"
  
  for t in "${all_times[@]}"; do
    # Calculate difference: (t - average)
    diff=$(echo "scale=4; $t - $average_time" | bc)
    # Calculate squared difference: (t - average)^2
    squared_diff=$(echo "scale=4; $diff * $diff" | bc)
    # Add to sum
    sum_of_squared_diffs=$(echo "scale=4; $sum_of_squared_diffs + $squared_diff" | bc)
  done

  # Variance = Sum of Squared Diffs / N
  variance=$(echo "scale=4; $sum_of_squared_diffs / $successful_runs" | bc)
  
  # Standard Deviation = sqrt(Variance)
  # 'bc -l' loads the math library, which includes sqrt()
  # We set the final scale to 2 decimal places
  std_dev=$(echo "scale=2; sqrt($variance)" | bc -l)

  echo "Variance:      ${variance} ms²"
  echo "Std Deviation: ${std_dev} ms"
else
  echo "No successful runs. Cannot calculate average."
fi