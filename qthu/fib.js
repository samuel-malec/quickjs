function fib( n )
{
    let a = 1;
    let b = 1;

    for ( var i = 0; i < n - 2; i++ )
    {
        var tmp = b;
        b = a + b;
        a = tmp;
    }

    return b;
}
