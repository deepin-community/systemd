#!/bin/bash
set -e

# Function to check if RTC is set to local time
is_rtc_local() {
    # Use grep to detect LOCAL keyword in /etc/adjtime
    if [ -f /etc/adjtime ] && grep -q "LOCAL" /etc/adjtime; then
        echo 1
    else
        echo 0
    fi
}

# Get hardware clock time as timestamp
get_hw_timestamp() {
    local is_local="$1"
    local hwtime_str
    local args=("-r")

    # Use -u if RTC is in UTC
    if [ "$is_local" -eq 0 ]; then
        args+=("-u")
    fi

    # Run hwclock and capture output
    if ! hwtime_str=$(hwclock "${args[@]}"); then
        echo "Failed to read hardware clock."
        return 1
    fi

    # Convert to timestamp
    local hw_timestamp
    if ! hw_timestamp=$(date -d "$hwtime_str" +%s 2>/dev/null); then
        echo "Failed to parse hardware clock output: $hwtime_str"
        return 1
    fi

    # Return the timestamp
    echo "$hw_timestamp"
}

# Main function to handle logic
main() {
    local is_local
    local hw_timestamp
    local system_timestamp
    local diff_sec

    # Detect if RTC uses local time
    is_local=$(is_rtc_local)

    # Get hardware clock timestamp
    if ! hw_timestamp=$(get_hw_timestamp "$is_local"); then
        echo "Skipping RTC adjustment due to error."
        return 1
    fi

    # Get current system timestamp
    if ! system_timestamp=$(date +%s 2>/dev/null); then
        echo "Failed to get system timestamp."
        return 1
    fi

    # Calculate difference
    diff_sec=$((hw_timestamp - system_timestamp))

    # If time difference is more than 5 seconds, write system time to RTC
    if [ "$diff_sec" -gt 5 ] || [ "$diff_sec" -lt -5 ]; then
        echo "Adjusting RTC..."
        hwclock -w
    fi
}

# Run main function with all script arguments
main "$@"
