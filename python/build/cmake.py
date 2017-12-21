import os.path, subprocess

from build.project import Project

class CMakeProject(Project):
    def __init__(self, url, md5, installed, configure_args=[], cppflags='', **kwargs):
        Project.__init__(self, url, md5, installed, **kwargs)
        self.configure_args = configure_args
        self.cppflags = cppflags

    def build(self, toolchain):
        src = self.unpack(toolchain)

        build = self.make_build_path(toolchain)
        
        # Build makefile.
        subprocess.check_call(['cmake', '-GUnix Makefiles', '-DCMAKE_INSTALL_PREFIX=' + toolchain.install_prefix, src] + self.configure_args, cwd=build)

        subprocess.check_call(['/usr/bin/make', '--quiet', '-j12'],
                              cwd=build, env=toolchain.env)
        subprocess.check_call(['/usr/bin/make', '--quiet', 'install'],
                              cwd=build, env=toolchain.env)
