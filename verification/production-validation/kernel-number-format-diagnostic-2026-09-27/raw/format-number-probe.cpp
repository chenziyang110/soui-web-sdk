// Diagnostic extraction: exact formatting body, with CRT classification adapters.
// This is not an instrumented complete libxml or SDK binary.
#include <cmath>
#include <cfloat>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
static int xmlXPathIsInf(double value) { return std::isinf(value) ? (std::signbit(value) ? -1 : 1) : 0; }
static int xmlXPathIsNaN(double value) { return std::isnan(value) ? 1 : 0; }
static int xmlXPathGetSign(double value) { return std::signbit(value) ? 1 : 0; }
#define UPPER_DOUBLE 1E9
#define LOWER_DOUBLE 1E-5
#define	LOWER_DOUBLE_EXP 5

#define INTEGER_DIGITS DBL_DIG
#define FRACTION_DIGITS (DBL_DIG + 1 + (LOWER_DOUBLE_EXP))
#define EXPONENT_DIGITS (3 + 2)

static void
xmlXPathFormatNumber(double number, char buffer[], int buffersize)
{
    switch (xmlXPathIsInf(number)) {
    case 1:
	if (buffersize > (int)sizeof("Infinity"))
	    snprintf(buffer, buffersize, "Infinity");
	break;
    case -1:
	if (buffersize > (int)sizeof("-Infinity"))
	    snprintf(buffer, buffersize, "-Infinity");
	break;
    default:
	if (xmlXPathIsNaN(number)) {
	    if (buffersize > (int)sizeof("NaN"))
		snprintf(buffer, buffersize, "NaN");
	} else if (number == 0 && xmlXPathGetSign(number) != 0) {
	    snprintf(buffer, buffersize, "0");
	} else if (number == ((int) number)) {
	    char work[30];
	    char *ptr, *cur;
	    int value = (int) number;

            ptr = &buffer[0];
	    if (value == 0) {
		*ptr++ = '0';
	    } else {
		snprintf(work, 29, "%d", value);
		cur = &work[0];
		while ((*cur) && (ptr - buffer < buffersize)) {
		    *ptr++ = *cur++;
		}
	    }
	    if (ptr - buffer < buffersize) {
		*ptr = 0;
	    } else if (buffersize > 0) {
		ptr--;
		*ptr = 0;
	    }
	} else {
	    /*
	      For the dimension of work,
	          DBL_DIG is number of significant digits
		  EXPONENT is only needed for "scientific notation"
	          3 is sign, decimal point, and terminating zero
		  LOWER_DOUBLE_EXP is max number of leading zeroes in fraction
	      Note that this dimension is slightly (a few characters)
	      larger than actually necessary.
	    */
	    char work[DBL_DIG + EXPONENT_DIGITS + 3 + LOWER_DOUBLE_EXP];
	    int integer_place, fraction_place;
	    char *ptr;
	    char *after_fraction;
	    double absolute_value;
	    int size;

	    absolute_value = fabs(number);

	    /*
	     * First choose format - scientific or regular floating point.
	     * In either case, result is in work, and after_fraction points
	     * just past the fractional part.
	    */
	    if ( ((absolute_value > UPPER_DOUBLE) ||
		  (absolute_value < LOWER_DOUBLE)) &&
		 (absolute_value != 0.0) ) {
		/* Use scientific notation */
		integer_place = DBL_DIG + EXPONENT_DIGITS + 1;
		fraction_place = DBL_DIG - 1;
		size = snprintf(work, sizeof(work),"%*.*e",
			 integer_place, fraction_place, number);
		while ((size > 0) && (work[size] != 'e')) size--;

	    }
	    else {
		/* Use regular notation */
		if (absolute_value > 0.0) {
		    integer_place = (int)log10(absolute_value);
		    if (integer_place > 0)
		        fraction_place = DBL_DIG - integer_place - 1;
		    else
		        fraction_place = DBL_DIG - integer_place;
		} else {
		    fraction_place = 1;
		}
		size = snprintf(work, sizeof(work), "%0.*f",
				fraction_place, number);
	    }

	    /* Remove fractional trailing zeroes */
	    after_fraction = work + size;
	    ptr = after_fraction;
	    while (*(--ptr) == '0')
		;
	    if (*ptr != '.')
	        ptr++;
	    while ((*ptr++ = *after_fraction++) != 0);

	    /* Finally copy result back to caller */
	    size = strlen(work) + 1;
	    if (size > buffersize) {
		work[buffersize - 1] = 0;
		size = buffersize;
	    }
	    memmove(buffer, work, size);
	}
	break;
    }
}

static unsigned long long cases = 0;
static size_t longest = 0;
static void check(double value) {
    char buffer[100];
    std::memset(buffer, 0x5a, sizeof buffer);
    xmlXPathFormatNumber(value, buffer, 99); // Exact buffer size of the only production caller.
    buffer[99] = 0;
    const size_t length = std::strlen(buffer);
    if (length > longest) longest = length;
    ++cases;
}
int main() {
    unsigned locales = 0;
    const double edge[] = {0.0, -0.0, 1.0, -1.0, DBL_MIN, -DBL_MIN, DBL_MAX, -DBL_MAX,
        std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        1e-5, -1e-5, std::nextafter(1e-5,0.0), std::nextafter(1e-5,1.0),
        1e9, -1e9, std::nextafter(1e9,0.0), std::nextafter(1e9,1e10),
        2147483647.0, -2147483648.0, 2147483648.0, -2147483649.0};
    for (const char* locale : {"C", "en-US", "zh-CN", "de-DE"}) {
        const char* selected = std::setlocale(LC_NUMERIC, locale);
        if (!selected) { std::printf("locale-unavailable: %s\n", locale); continue; }
        ++locales; std::printf("locale: %s; decimal: %s\n", selected, std::localeconv()->decimal_point);
        for (double value : edge) check(value);
        for (int exponent=-1074; exponent<=1023; ++exponent) {
            const double value=std::ldexp(1.0,exponent);
            check(value); check(-value); check(std::nextafter(value,0.0)); check(std::nextafter(value,HUGE_VAL));
        }
        std::uint64_t state=UINT64_C(0x84222325cbf29ce4);
        for (unsigned i=0;i<200000;++i) {
            state^=state<<13; state^=state>>7; state^=state<<17;
            double value; std::memcpy(&value,&state,sizeof value); check(value);
        }
    }
    std::printf("format-cases=%llu; locales=%u; longest-output=%zu\n",cases,locales,longest);
    std::puts("scope=extracted-formatting-body; full-kernel-security-verified=false");
    return locales==4 ? 0 : 2;
}
