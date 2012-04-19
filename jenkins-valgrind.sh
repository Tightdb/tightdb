make debug
make -C test debug # Runs with DEBUG enabled
make -C test/test-tightdb # Runs with DEBUG disabled (because it would take forever)

#valgrind --leak-check=full --xml=yes --xml-file=valgrind.xml test/tightdb-tests-debug --no-error-exit-staus
echo valgrind > valgrind.csv
(echo 0; cat valgrind.xml | perl -ne 'if (m/lost in loss record \d+ of (\d+)/){ print "$1\n"; }') | tail -1 >> valgrind.csv

valgrind --leak-check=full --xml=yes --xml-file=valgrind_test.xml test/test-tightdb/test-tightdb
echo valgrind_test > valgrind_test.csv
(echo 0; cat valgrind_test.xml | perl -ne 'if (m/lost in loss record \d+ of (\d+)/){ print "$1\n"; }') | tail -1 >> valgrind_test.csv
