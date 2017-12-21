import os, shutil, subprocess

def untar(tarball_path, parent_path, base):
    path = os.path.join(parent_path, base)
    try:
        shutil.rmtree(path)
    except FileNotFoundError:
        pass
    os.makedirs(parent_path, exist_ok=True)
    bin = '/bin/tar'
    if not os.path.isfile(bin):
        bin = '/usr/bin/tar'
    
    subprocess.check_call([bin, 'xfC', tarball_path, parent_path])
    return path
