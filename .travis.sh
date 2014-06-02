find . -type d -name "*.xcodeproj" | while read i
do
	for j in Release Debug
	do
		xcodebuild -project "$i" -configuration $j
	done
done
for i in `find . -name "*.kext" -type d`
do
	echo "Trying $i"
	sudo chown -R root:wheel $i && sudo kextutil $i
done
