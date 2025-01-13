function with_scale() {
	export LIBVA_DRIVER_NAME=iHD
	./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA --add-scale --sink fakesink
	./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA --add-scale --sink xvimagesink
	./multistream_decode.sh --num-of-sources 10 --show-fps --format I420 --add-scale --sink fakesink
	./multistream_decode.sh --num-of-sources 10 --show-fps --format I420 --add-scale --sink xvimagesink

	export LIBVA_DRIVER_NAME=fakedriver
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format RGBA --add-scale --sink fakesink
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format RGBA --add-scale --sink xvimagesink
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format I420 --add-scale --sink fakesink
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format I420 --add-scale --sink xvimagesink
}


function without_scale() {
	export LIBVA_DRIVER_NAME=iHD
	./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA --sink fakesink
	./multistream_decode.sh --num-of-sources 10 --show-fps --format RGBA --sink xvimagesink
	./multistream_decode.sh --num-of-sources 10 --show-fps --format I420 --sink fakesink
	./multistream_decode.sh --num-of-sources 10 --show-fps --format I420 --sink xvimagesink

	export LIBVA_DRIVER_NAME=fakedriver
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format RGBA --sink fakesink
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format RGBA --sink xvimagesink
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format I420 --sink fakesink
	./multistream_decode_cpu.sh --num-of-sources 10 --show-fps --format I420 --sink xvimagesink
}

without_scale
