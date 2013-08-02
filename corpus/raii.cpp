#include <stdexcept>


struct incrementor
{
	int& _value;
	incrementor(int& v) : _value( v ) {}
	~incrementor() { --_value; }
};

int main( int c, char** v )
{
	int test = 10;
	incrementor testinc( test );
	test += 5;
	try
	{
		if ( test % 5 == 0 )
		{
			incrementor test2( test );
			if ( test )
				throw std::runtime_error( "err" );
		}
	}
	catch( std::logic_error& le )
	{
		test += 20;
	}
	catch( std::runtime_error& re )
	{
		test -= 10;
	}
	catch( ... )
	{
		test = test * 5;
	}
	return 1;
}
