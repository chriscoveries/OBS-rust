#!/usr/bin/env bash
# Checks the recording that test-appimage.sh made with a saved profile:
#   verify-recording.sh <recording.mkv>
#   verify-recording.sh --self-test     (proves the checks fail on bad input)
# Runs outside the test container (it needs ffmpeg, which the container must
# not have). The scene is a PipeWire screen capture filling the 320x240 canvas
# with a camera in the bottom-right 80x60 corner, both fed moving test
# patterns, plus a microphone fed a 440 Hz tone. So: the file decodes cleanly
# at 320x240 with enough frames, both picture regions keep changing, and the
# audio is the tone throughout. Every check fails closed: a tool error is a
# failure, never a pass.
set -euo pipefail

command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null || {
    echo "::error::ffmpeg and ffprobe are required"
    exit 1
}

# Count lines matching $1 in stdin; 0 matches is a valid answer, a read error is not.
count() {
    local n
    n="$(grep -c -- "$1")" || [ "$?" -eq 1 ]
    echo "${n:-0}"
}

verify() {
    local rec="$1" fail=0 out duration size frames n all tone silences
    error() {
        echo "::error::$*"
        fail=1
    }

    if ! out="$(ffmpeg -nostdin -hide_banner -v error -i "$rec" -f null - 2>&1)" || [ -n "$out" ]; then
        echo "$out" | head -5
        error "the recording does not decode cleanly"
        return 1
    fi
    duration="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$rec")"
    size="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$rec")"
    frames="$(ffprobe -v error -select_streams v:0 -count_frames -show_entries stream=nb_read_frames -of csv=p=0 "$rec")"
    out="$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$rec")"
    echo "duration: $duration s, video: $size, $frames frames, audio: ${out:-none}"
    [ "$size" = 320x240 ] || error "the video is ${size:-missing}, not the 320x240 canvas"
    [ "${frames:-0}" -ge 150 ] 2>/dev/null || error "the video has ${frames:-no} frames, fewer than 5 s at 30 fps"
    [ -n "$out" ] || error "the recording has no audio stream"
    [ "$fail" -eq 0 ] || return 1

    # Moving pictures: a stalled capture shows up as a freeze. The first second
    # is skipped (sources connect while the recording starts).
    for region in screen:240:180:0:0 camera:80:60:240:180; do
        out="$(ffmpeg -nostdin -hide_banner -ss 1 -i "$rec" -an \
            -vf "crop=${region#*:},freezedetect=n=0.001:d=1" -f null - 2>&1)" ||
            { error "ffmpeg failed on the ${region%%:*} region"; continue; }
        n="$(count freeze_start <<<"$out")"
        echo "${region%%:*} region: $n freezes $(grep -o 'freeze_start: [0-9.]*' <<<"$out" | tr '\n' ' ')"
        [ "$n" -eq 0 ] || error "the ${region%%:*} picture froze ($n times)"
    done

    # The tone: loud enough, never silent, and mostly at 440 Hz.
    rms() {
        local o
        o="$(ffmpeg -nostdin -hide_banner -ss 1 -i "$rec" -vn -af "${1}astats=measure_perchannel=none" -f null - 2>&1)" || return 1
        sed -n 's/.*RMS level dB: *\(-*[0-9.inf]*\).*/\1/p' <<<"$o" | tail -1
    }
    all="$(rms '')" || error "ffmpeg failed to measure the audio"
    tone="$(rms 'bandpass=f=440:width_type=h:w=100,')" || error "ffmpeg failed to measure the audio at 440 Hz"
    echo "audio RMS: ${all:-?} dB, at 440 Hz: ${tone:-?} dB"
    awk -v a="${all:-x}" 'BEGIN{exit !(a ~ /^-?[0-9.]+$/ && a > -40)}' || error "the microphone is silent or nearly so (RMS ${all:-?} dB)"
    awk -v a="${all:-x}" -v t="${tone:-x}" 'BEGIN{exit !(t ~ /^-?[0-9.]+$/ && a - t < 3)}' ||
        error "the audio is not the 440 Hz test tone (RMS ${all:-?} dB, ${tone:-?} dB at 440 Hz)"
    out="$(ffmpeg -nostdin -hide_banner -ss 1 -i "$rec" -vn -af silencedetect=noise=-50dB:d=0.3 -f null - 2>&1)" ||
        error "ffmpeg failed to look for silence"
    silences="$(count silence_start <<<"$out")"
    echo "audio: $silences silences"
    [ "$silences" -eq 0 ] || error "the microphone dropped out ($silences silences)"

    [ "$fail" -eq 0 ]
}

if [ "${1:-}" = --self-test ]; then
    # A good recording must pass, and a frozen screen, a silent microphone and
    # a file that is not a recording must each fail.
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' EXIT
    make() { # make <file> <screen filter> <audio filter>
        ffmpeg -nostdin -v error -f lavfi -i "$2" -f lavfi -i 'testsrc2=s=80x60:r=30' \
            -f lavfi -i "$3" -filter_complex '[0][1]overlay=240:180[v]' -map '[v]' -map 2 -t 8 \
            -c:v libx264 -pix_fmt yuv420p -c:a aac "$1"
    }
    make "$dir/good.mkv" 'testsrc2=s=320x240:r=30' 'sine=f=440:r=48000'
    make "$dir/frozen.mkv" 'testsrc2=s=320x240:r=30,trim=end_frame=1,tpad=stop=-1:stop_mode=clone' 'sine=f=440:r=48000'
    make "$dir/silent.mkv" 'testsrc2=s=320x240:r=30' 'anullsrc=r=48000:cl=stereo'
    head -c 100000 /dev/urandom >"$dir/garbage.mkv"
    ok=1
    for case in good:0 frozen:1 silent:1 garbage:1; do
        echo "== self-test: ${case%%:*} (expect $([ "${case#*:}" = 0 ] && echo pass || echo fail))"
        set +e
        (verify "$dir/${case%%:*}.mkv")
        rc=$?
        set -e
        if { [ "${case#*:}" = 0 ] && [ "$rc" -ne 0 ]; } || { [ "${case#*:}" = 1 ] && [ "$rc" -eq 0 ]; }; then
            echo "::error::verify-recording.sh self-test: the ${case%%:*} case came out wrong"
            ok=0
        fi
    done
    [ "$ok" -eq 1 ] || exit 1
    echo "verify-recording: self-test passed (good passes; frozen, silent and garbage fail)"
    exit 0
fi

if verify "$1"; then
    echo "verify-recording: screen and camera move, the microphone carries the 440 Hz tone throughout"
else
    echo "verify-recording: FAILED"
    exit 1
fi
