#include "CuTest.h"

void TestModelLoadSave(CuTest *t)
{
	static const char input[] = "Configure";
	const char *loc;
    chdir(FILESYSTEM_DIR);

    // for default language, input == loc
    CONFIG_ReadLang(0);
    CuAssertStrEquals(t, _tr(input), input);

    // loc string is not equals to input
    CONFIG_ReadLang(1);
    CuAssertTrue(t, strcmp(_tr(input), input) != 0);
    loc = _tr(input);

    // After switch language, the text should be different
    CONFIG_ReadLang(2);
    CuAssertTrue(t, strcmp(_tr(input), loc) != 0);
}