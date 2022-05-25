#ifdef TEST_COMM // Testing network
    // Both SSID and password must be 8 characters or longer
    #define SECRET_SSID ""
    #define SECRET_PASS ""

    // Replace with your network credentials
    #ifndef STASSID
        #define STASSID ""
        #define STAPSK  ""
    #endif

#else // Production network

    // Both SSID and password must be 8 characters or longer

    #define SECRET_SSID ""
    #define SECRET_PASS ""

    // Replace with your network credentials
    #ifndef STASSID
        #define STASSID ""
        #define STAPSK  ""
    #endif
#endif 

#define SERVICE_NAME    ""

#define REF_CNTLR_DEV   1

const char* REF_CNTLR =     "01"; 
const char* REF_CNTLR_NAME =    "RefCtl";


