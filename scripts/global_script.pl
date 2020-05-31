use List::Util qw[min max];


$it_sov = 1000000;
$it_mv = 1000;
$it_sov_step = 1000000, $it_mv_step = 1000;
$it_sov_max = 100000000, $it_mv_max = 15000;
$min_proc = 2, $max_proc = 4;
while($it_sov < $it_sov_max || $it_mv < $it_mv_max) {
    if ($it_sov < $it_sov_max) {
        open(WF, "> ./output_parallel_reduce_sum_of_vector.txt") or die;    
        for ($j = $min_proc; $j <= $max_proc; $j = $j*2) {
            $result = 250000.0;
            for ($k = 0; $k < 3; $k++) {
                $tmp = `mpiexec -n $j ../build/Release/parallel_reduce_sum_of_vector $it_sov`;
                $result = min($result + 0.0, $tmp+0.0);
            }
            print WF2 $result;
            print WF2 " ";
        }
        print "parallel_reduce for $it_sov\n";
        $it_sov += $it_sov_step;
        close(WF);
    }
    if ($it_mv < $it_mv_max) {
        open(WF2, "> ./output_reduce_multiple_vector_matrix.txt") or die;
        for ($j = $min_proc; $j <= $max_proc; $j = $j*2) {
            $result = 250000.0;
            for ($k = 0; $k < 3; $k++) {
                $tmp = `mpiexec -n $j ../build/Release/matrixvector $it_mv $it_mv`;
                $result = min($result + 0.0, $tmp+0.0);
            }
            print WF2 $result;
            print WF2 " ";
        }
        print WF2 "\n";
        print "matrixvector for $it_mv\n";
        $it_mv += $it_mv_step;
        close(WF2);
    }
}

