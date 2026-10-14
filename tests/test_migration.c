#define main controller_tests_unused
#include "test_controller.c"
#undef main
static char *copy(const char *value){char *p=malloc(strlen(value)+1);strcpy(p,value);return p;}
int main(void){
    vpn_private_key=copy(TEST_KEY);vpn_public_key=copy(TEST_KEY);vpn_preshared_key=copy("");
    vpn_endpoint=copy("vpn.example.com");vpn_address=copy("10.2.0.2");vpn_netmask=copy("255.255.255.255");vpn_dns=copy("10.2.0.1");
    vpn_enabled=1;assert(vpn_profiles_boot()==ESP_OK);
    assert(selected==0 && desired && using_profile && vpn_profile_guard);
    vpn_profile_t original,cloned;
    assert(vpn_profile_load(0,&original)==ESP_OK && !strcmp(original.name,"Imported VPN"));
    int id=-1;assert(vpn_profile_clone(0,"TestCopy",&id)==ESP_OK && id==1);
    assert(vpn_profile_load(1,&cloned)==ESP_OK && !strcmp(cloned.name,"TestCopy") && !strcmp(original.private_key,cloned.private_key));
    assert(vpn_profile_select(1,true)==ESP_OK);step();step();assert(live_tunnels==1 && selected==1);
    assert(vpn_profile_stop()==ESP_OK);int stored=-1;bool connect=true;
    assert(vpn_selection_load(&stored,&connect)==ESP_OK && stored==1 && !connect);
    assert(vpn_profile_select(0,true)==ESP_OK);step();step();assert(live_tunnels==1 && max_tunnels==1);
    assert(vpn_profile_remove(1)==ESP_OK);
    puts("Migration/clone tests passed: complete legacy config becomes a protected profile, clone retains write-only secrets, selection survives reload.");return 0;
}
