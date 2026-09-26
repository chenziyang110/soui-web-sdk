'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',root=__dirname;
const source='third_party/sources/miniblink/third_party/libxml/src/xpath.c',original=fs.readFileSync(path.join(repo,source)),text=original.toString('utf8').replaceAll('\r\n','\n');
const matches=[...text.matchAll(/static void\nxmlXPathFormatNumber[\s\S]*?\n}\n/g)];assert.equal(matches.length,1);
const start=text.indexOf('#define UPPER_DOUBLE'),end=text.indexOf('/**\n * xmlXPathFormatNumber:',start);assert(start>0&&end>start);
const extracted=text.slice(start,end)+matches[0][0];
const prefix=`// Diagnostic extraction: exact formatting body, with CRT classification adapters.
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
`;
const suffix=`
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
        if (!selected) { std::printf("locale-unavailable: %s\\n", locale); continue; }
        ++locales; std::printf("locale: %s; decimal: %s\\n", selected, std::localeconv()->decimal_point);
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
    std::printf("format-cases=%llu; locales=%u; longest-output=%zu\\n",cases,locales,longest);
    std::puts("scope=extracted-formatting-body; full-kernel-security-verified=false");
    return locales==4 ? 0 : 2;
}
`;
fs.writeFileSync(path.join(root,'format-number-probe.cpp'),prefix+extracted+suffix,{flag:'wx'});
fs.copyFileSync(path.join(repo,'third_party/sources/miniblink/third_party/libxml/src/Copyright'),path.join(root,'libxml-Copyright.txt'),fs.constants.COPYFILE_EXCL);
fs.writeFileSync(path.join(root,'probe-input.json'),JSON.stringify({source,sourceSha256:crypto.createHash('sha256').update(original).digest('hex'),bodySha256:crypto.createHash('sha256').update(extracted).digest('hex'),generatedSha256:crypto.createHash('sha256').update(prefix+extracted+suffix).digest('hex'),classificationAdapters:'std::isinf, std::isnan, std::signbit; not actual trio implementation',productionBufferSize:99,extractedBodyOnly:true,fullKernelSecurityVerified:false},null,2)+'\n',{flag:'wx'});
