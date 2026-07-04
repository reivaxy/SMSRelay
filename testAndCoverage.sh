pio test -e tests

gcovr `
   --root . `
   --filter src/ `
   --object-directory .pio/build/tests `
   --exclude-directories ".pio/libdeps" `
   --html --html-details -o test/coverage/coverage.html

echo "Coverage report generated at test/coverage/coverage.html"