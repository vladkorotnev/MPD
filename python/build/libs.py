from build.project import Project
from build.zlib import ZlibProject
from build.autotools import AutotoolsProject
from build.ffmpeg import FfmpegProject
from build.boost import BoostProject
from build.cmake import CMakeProject

libogg = AutotoolsProject(
    'http://downloads.xiph.org/releases/ogg/libogg-1.3.2.tar.xz',
    '5c3a34309d8b98640827e5d0991a4015',
    'lib/libogg.a',
    ['--disable-shared', '--enable-static'],
)

libvorbis = AutotoolsProject(
    'http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.5.tar.xz',
    '28cb28097c07a735d6af56e598e1c90f',
    'lib/libvorbis.a',
    ['--disable-shared', '--enable-static'],
)

opus = AutotoolsProject(
    'http://downloads.xiph.org/releases/opus/opus-1.1.4.tar.gz',
    '9122b6b380081dd2665189f97bfd777f04f92dc3ab6698eea1dbb27ad59d8692',
    'lib/libopus.a',
    ['--disable-shared', '--enable-static'],
)

flac = AutotoolsProject(
    'http://downloads.xiph.org/releases/flac/flac-1.3.2.tar.xz',
    '91cfc3ed61dc40f47f050a109b08610667d73477af6ef36dcad31c31a4a8d53f',
    'lib/libFLAC.a',
    [
        '--disable-shared', '--enable-static',
        '--disable-xmms-plugin', '--disable-cpplibs',
    ],
)

zlib = ZlibProject(
    'http://zlib.net/zlib-1.2.11.tar.xz',
    '4ff941449631ace0d4d203e3483be9dbc9da454084111f97ea0a2114e19bf066',
    'lib/libz.a',
)

libid3tag = AutotoolsProject(
    'ftp://ftp.mars.org/pub/mpeg/libid3tag-0.15.1b.tar.gz',
    'e5808ad997ba32c498803822078748c3',
    'lib/libid3tag.a',
    ['--disable-shared', '--enable-static'],
    autogen=False,
)

libmad = AutotoolsProject(
    'ftp://ftp.mars.org/pub/mpeg/libmad-0.15.1b.tar.gz',
    '1be543bc30c56fb6bea1d7bf6a64e66c',
    'lib/libmad.a',
    ['--disable-shared', '--enable-static'],
    autogen=True,
)

liblame = AutotoolsProject(
    'http://downloads.sourceforge.net/project/lame/lame/3.99/lame-3.99.5.tar.gz',
    '24346b4158e4af3bd9f2e194bb23eb473c75fb7377011523353196b19b9a23ff',
    'lib/libmp3lame.a',
    [
        '--disable-shared', '--enable-static',
        '--disable-gtktest', '--disable-analyzer-hooks',
        '--disable-decoder', '--disable-frontend',
    ],
)

ffmpeg = FfmpegProject(
    'http://ffmpeg.org/releases/ffmpeg-3.3.2.tar.xz',
    '1998de1ab32616cbf2ff86efc3f1f26e76805ec5dc51e24c041c79edd8262785',
    'lib/libavcodec.a',
    [
        '--disable-shared', 
		'--enable-static',
        '--enable-gpl',
        '--enable-small',
        #'--disable-pthreads',
        '--disable-programs',
        '--disable-doc',
        '--disable-avdevice',
        '--disable-swresample',
        '--disable-swscale',
        '--disable-postproc',
        '--disable-avfilter',
        '--disable-outdevs',
        '--disable-encoders',
        '--disable-filters',
		'--disable-iconv',
		'--disable-bzlib',
		'--disable-lzma'
    ],
)

curl = AutotoolsProject(
    'https://curl.haxx.se/download/curl-7.57.0.tar.bz2',
    'c92fe31a348eae079121b73884065e600c533493eb50f1f6cee9c48a3f454826',
    'lib/libcurl.a',
    [
        '--disable-shared', '--enable-static',
        '--disable-debug',
        '--enable-http', '--without-nghttp2',
        '--enable-ipv6',
        '--disable-ftp', '--disable-file',
        '--disable-ldap', '--disable-ldaps',
        '--disable-rtsp', '--disable-proxy', '--disable-dict', '--disable-telnet',
        '--disable-tftp', '--disable-pop3', '--disable-imap', '--disable-smtp', '--without-librtmp',
        '--disable-gopher',
        '--disable-manual',
        '--disable-threaded-resolver', '--disable-verbose', '--disable-sspi',
        '--disable-crypto-auth', '--disable-ntlm-wb', '--disable-tls-srp', '--disable-cookies',
        '--without-ssl', '--without-gnutls', '--without-nss', '--without-libssh2',
    ],
)

boost = BoostProject(
    'http://downloads.sourceforge.net/project/boost/boost/1.64.0/boost_1_64_0.tar.bz2',
    '7bcc5caace97baa948931d712ea5f37038dbb1c5d89b43ad4def4ed7cb683332',
    'include/boost/version.hpp',
)

libsamplerate = AutotoolsProject(
    'http://www.mega-nerd.com/SRC/libsamplerate-0.1.9.tar.gz',
    '0a7eb168e2f21353fb6d84da152e4512126f7dc48ccb0be80578c565413444c1',
    'lib/libsamplerate.a',
    ['--disable-shared', '--enable-static'],
    autogen=False,       
)

libsoundio = CMakeProject(
    'https://github.com/elan/libsoundio/archive/master.tar.gz',
    '1731ccc4799fdc94549b03093cb4f7c9f3e617fab0641b091f62b3a8cc246062',
    'lib/libsoundio.a',
    ['-DBUILD_TESTS=OFF', '-DBUILD_EXAMPLE_PROGRAMS=OFF', '-DBUILD_STATIC_LIBS=ON', '-DBUILD_DYNAMIC_LIBS=OFF'],
    name='libsoundio',
    version='921195a',
    base='libsoundio-master'
)

fftw = AutotoolsProject(
    'http://www.fftw.org/fftw-3.3.6-pl2.tar.gz',
    'a5de35c5c824a78a058ca54278c706cdf3d4abba1c56b63531c2cb05f5d57da2',
    'lib/libfftw3f.a',
    [
        '--enable-static', '--disable-shared', 
         '--enable-sse', '--enable-sse2', '--enable-avx', '--enable-avx2', '--enable-single',
         '--with-our-malloc'
    ],
    name='fftw',
    version='3.3.6-pl2'
)
