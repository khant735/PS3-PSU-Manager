#include "filen_json.h"
#include <assert.h>
int main(){FilenJsonReader r;FilenJson j;
 assert(r.parse("{\"status\":true,\"data\":{\"name\":\"a\\u002epsu\",\"chunks\":2}}",j));
 assert(j.get("status").type=='t');assert(j.get("data").get("name").text=="a.psu");
 assert(!r.parse("{\"x\":1,\"x\":2}",j));assert(!r.parse("{\"x\":01}",j));
 assert(!r.parse("{\"x\":\"\\uD800\"}",j));assert(!r.parse("{} trailing",j));
 assert(!r.parse("[1,]",j));assert(!r.parse("{\"a\":true,}",j));
 assert(r.parse("{\"x\":1.5,\"ignored\":[null,false,{},[]]}",j));return 0;}
