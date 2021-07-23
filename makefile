all:
	gcc aqg.c qrcodegen.c -o aqg
	rm -f out.gif
	echo '<html><head>' > i.html
	echo '<meta name="viewport" content="width=device-width, initial-scale=1.0">' >> i.html
	echo '<style>img {' >> i.html
	echo '	image-rendering: pixelated;' >> i.html
	echo '}' >> i.html
	echo 'body {' >> i.html
	echo '  margin: 0;' >> i.html
	echo '	padding:1em;' >> i.html
	echo '}' >> i.html
	echo '</style></head>' >> i.html
	echo '<body><center><img src="data:image/gif;base64,' >> i.html
	curl https://xpop.xrplf.org/D2380144F9A7D1CB77AFF43AAAD8DAF613E5A56026C90FC49C81B0576BEDF212 | jq -c -M '.' | ./aqg | base64 | tr -d '\n' >> i.html
	echo '"></center></body>' >> i.html
	./server.sh
