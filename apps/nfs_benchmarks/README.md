# nfs_benchmarks
benchmarks for AOS2015 nfs

follow the instructions here:
http://www.cse.unsw.edu.au/~cs9242/15/project/m7.shtml

namely:

- Modify the top level Kconfig file, adding the following line:
'''
source "apps/newapp/Kconfig"
'''
- Modify the apps/newapp/Kbuild file, to look like:
'''
apps-$(CONFIG_APP_NEWAPP) += newapp
newapp: $(libc) libsel4 libsos
'''
- Modify the apps/sos/Kbuild file to know about your new application and build it into the SOS cpio archive. Add the following line (where intuitively appropriate):
'''
sos-components-$(CONFIG_APP_NEWAPP) += newapp
'''
Lastly, you need to include your new application in the build process by selecting it in the configuration menu.
