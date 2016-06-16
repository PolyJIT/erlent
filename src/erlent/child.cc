extern "C" {
#include <fcntl.h>
#include <grp.h>
#include <pty.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
}

#include "erlent/child.hh"
#include "erlent/erlent.hh"

#include <sstream>

using namespace std;
using namespace erlent;

int tochild[2], toparent[2];

static void signal_child(char c) {
    if (write(tochild[1], &c, 1) == -1)
        perror("signal_child");
}

static void signal_parent(char c) {
    if (write(toparent[1], &c, 1) == -1)
        perror("signal_parent");
}

static void wait_child(char expected) {
    char c = 0;
    if (read(toparent[0], &c, 1) == -1)
        perror("wait_child");
    if (c != expected) {
        cerr << "Communication error in wait_child: expected '" << expected
             << "', got '" << c << "'." << endl;
        exit(1);
    }
}

static void wait_parent(char expected) {
    char c = 0;
    if (read(tochild[0], &c, 1) == -1)
        perror("wait_parent");
    if (c != expected) {
        cerr << "Communication error in wait_parent: expected '" << expected
             << "', got '" << c << "'." << endl;
        exit(1);
    }
}


static void mnt(const char *src, const char *dst, const char *fstype, int flags=0,
         const char *options=NULL) {
    int res = mount(src, dst, fstype, flags, options);
    if (res == -1) {
        int err = errno;
        cerr << "Mount of '" << src << "' at '" << dst << "' (type " << fstype
             << ") failed: " << strerror(err) << endl;
        exit(1);
    }
}

// Handler for PTY resize (SIGWINCH).
int amaster;  // master device of child's PTY
static void sigwinch_action(int sig, siginfo_t *, void *) {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, (char *) &ws) < 0) {
        cerr << "TIOCGWINSZ error" << endl;
    }
    if (ioctl(amaster, TIOCSWINSZ, (char *) &ws) < 0) {
        cerr << "TIOCSWINSZ error" << endl;
    }
}

static char *const *args;
extern int pivot_root(const char * new_root,const char * put_old);
static int childFunc(ChildParams params)
{
    wait_parent('I');

    dbg() << "newroot = " << params.newRoot << endl;
    dbg() << "newwd = " << params.newWorkDir << endl;
    dbg() << "command = ";
    for (char *const *p=args; *p; ++p) {
        dbg() << " " << *p;
    }
    dbg() << endl;

    // make newRoot a mount point so we can use pivot_root later
//    mnt(params.newRoot.c_str(), params.newRoot.c_str(), MS_BIND | MS_REC);
    if (params.devprocsys) {
        const string &root = params.newRoot;
        mnt("/dev", (root+"/dev").c_str(), NULL, MS_BIND | MS_REC);
        mnt("/sys", (root+"/sys").c_str(), NULL, MS_BIND | MS_REC);
    }
    for (const pair<string,string> &b : params.bindMounts) {
        mnt(b.first.c_str(), (params.newRoot+"/"+b.second).c_str(), NULL, MS_BIND | MS_REC);
    }

    if (chroot(params.newRoot.c_str()) == -1) {
        int err = errno;
        cerr << "chroot failed: " << strerror(err) << endl;
        exit(1);
    }

    if (params.devprocsys) {
        mnt("proc", "/proc", "proc");
        mnt("erlentpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,gid=5");
        mnt("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND);
        if (chmod("/dev/pts/ptmx", 0666) == -1) {
            int err = errno;
            cerr << "chmod on /dev/pts/ptmx failed: " << strerror(err) << endl;
            exit(1);
        }
    }
#if 0
    chdir(params.newRoot.c_str());
    if (syscall(__NR_pivot_root, ".", "./mnt") == -1) {
        int err = errno;
        cerr << "pivot_root failed: " << strerror(err) << endl;
        exit(1);
    }
    chdir("/");
#endif
    if (chdir(params.newWorkDir.c_str()) == -1) {
        int err = errno;
        cerr << "chdir failed: " << strerror(err) << endl;
        exit(1);
    }

#if 0
    cerr << "cwd = " << get_current_dir_name() << endl;

    DIR *d = opendir("/");
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        cerr << de->d_name << endl;
    }
    closedir(d);
#endif

#if 0
    if (dup2(2, 1) == -1) {
        int err = errno;
        cerr << "dup2 failed: " << strerror(err) << endl;
    }
#endif

    if (setreuid(params.initialUID, params.initialUID) == -1) {
        int err = errno;
        cerr << "setreuid failed: " << strerror(err) << endl;
        exit(1);
    }
    if (setregid(params.initialUID, params.initialGID) == -1) {
        int err = errno;
        cerr << "setregid failed: " << strerror(err) << endl;
        exit(1);
    }
    setgroups(0, NULL);

    if (isatty(0)) {
        char slave[200];
        pid_t p;
        struct winsize ws;
        struct termios oldsettings;
        if (tcgetattr(0, &oldsettings) == -1) {
            int err = errno;
            cerr << "tcgetattr failed: " << strerror(err) << endl;
            exit(127);
        }
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, (char *) &ws) < 0) {
            cerr << "TIOCGWINSZ error" << endl;
            exit(127);
        }

        // Make sure the signal handler for
        // SIGCHLD is _not_ SIG_IGN because in this case
        // waitpid can fail with ECHLD (there is no child
        // any more to wait for).
        signal(SIGCHLD, SIG_DFL);

        p = forkpty(&amaster, slave, &oldsettings, &ws);
        if (p == -1) {
            int err = errno;
            cerr << "forkpty failed: " << strerror(err) << endl;
            exit(127);
        } else if (p == 0) {
            execvp(args[0], args);
            int err = errno;
            cerr << "Could not execute '" << args[0] << "': " << strerror(err) << endl;
            exit(127);
        } else {
            // cerr << "slave device: " << slave << endl;

            // Install handler for terminal resizes.
            struct sigaction sact;
            memset(&sact, 0, sizeof(sact));
            sact.sa_sigaction = sigwinch_action;
            sact.sa_flags = SA_SIGINFO;
            sigaction(SIGWINCH, &sact, NULL);

            struct termios settings;
            // Set RAW mode on slave side of PTY
            settings = oldsettings;
            cfmakeraw(&settings);
            if (tcsetattr(0, TCSANOW, &settings) < 0) {
                int err = errno;
                cerr << "tcsetattr failed: " << strerror(err) << endl;
                exit(127);
            }

            bool quit = false;
            while (!quit) {
                char input[256];
                fd_set fd_in;

                FD_ZERO(&fd_in);
                FD_SET(0, &fd_in);
                FD_SET(amaster, &fd_in);

                int rc;
                do {
                    rc = select(amaster + 1, &fd_in, NULL, NULL, NULL);
                } while (rc == -1 && errno == EINTR);

                if (rc == -1) {
                    int err = errno;
                    cerr << "select failed: " << strerror(err) << endl;
                    tcsetattr(0, TCSANOW, &oldsettings);
                    exit(127);
                }

                if (FD_ISSET(0, &fd_in)) {
                    rc = read(0, input, sizeof(input));
                    if (rc > 0) {
                        if (write(amaster, input, rc) == -1)
                            quit = true;
                    } else if (rc <= 0)
                        quit = true;
                }

                if (FD_ISSET(amaster, &fd_in)) {
                    rc = read(amaster, input, sizeof(input));
                    if (rc > 0) {
                        if (write(1, input, rc) == -1)
                            quit = true;
                    } else if (rc <= 0)
                        quit = true;
                }
            }
            tcsetattr(0, TCSANOW, &oldsettings);
            int res = wait_for_pid(p);
            exit(res);
        }
    } else { // stdin is not a terminal
        execvp(args[0], args);
        int err = errno;
        cerr << "Could not execute '" << args[0] << "': " << strerror(err) << endl;
        return 127;
    }
}

static void initComm() {
    if (pipe(tochild) == -1)
        perror("pipe/tochild");
    if (pipe(toparent) == -1)
        perror("pipe/toparent");
}

int uidmap_single(pid_t child_pid, uid_t new_uid, gid_t new_gid) {
    char str[200];
    int fd;
    uid_t euid = geteuid();
    gid_t egid = getegid();

    // gid_map can only be written when setgroups
    // is disabled on newer kernels (for security
    // reasons).
    sprintf(str, "/proc/%d/setgroups", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1) {
        perror("open /proc/.../setgroups");
        return -1;
    }
    sprintf(str, "deny");
    if (write(fd, str, strlen(str)) == -1) {
        perror("write setgroups");
        return -1;
    }
    close(fd);

    sprintf(str, "/proc/%d/uid_map", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1) {
        perror("open uid_map");
        return -1;
    }

    sprintf(str,"%ld %ld 1\n", (long)new_uid, (long)euid);
    if (write(fd, str, strlen(str)) == -1) {
        perror("write uid");
        return -1;
    }
    close(fd);

    sprintf(str, "/proc/%d/gid_map", child_pid);
    fd = open(str, O_WRONLY);
    if (fd == -1) {
        perror("open gid_map");
        return -1;
    }

    sprintf(str,"%ld %ld 1\n", (long)new_gid, (long)egid);
    if (write(fd, str, strlen(str)) == -1) {
        perror("write gid");
        return -1;
    }
    close(fd);
    return 0;
}

int do_idmap(const char *cmd, pid_t child_pid, const std::vector<Mapping> &mappings) {
    ostringstream oss;
    oss << cmd << " " << dec << child_pid;
    for (const Mapping &m : mappings) {
        oss << " " << dec << m.innerID << " " << m.outerID << " " << m.count;
    }
    int res = system(oss.str().c_str());
    if (res == -1) {
        cerr << oss.str() << ": cannot invoke (error -1) ?!" << endl;
        return -1;
    } else if (WIFEXITED(res)) {
        if (WEXITSTATUS(res) != 0) {
            cerr << oss.str() << " failed: exit status " << WEXITSTATUS(res) << endl;
            return -1;
        }
    } else {
        cerr << oss.str() << " failed, no exit status." << endl;
        return -1;
    }

    return 0;
}

int uidmap_sub(pid_t child_pid, const ChildParams &params) {
    if (do_idmap("newuidmap", child_pid, params.uidMappings) != 0)
        return -1;
    if (do_idmap("newgidmap", child_pid, params.gidMappings) != 0)
        return -1;
    return 0;
}

static int child_res;

pid_t erlent::setup_child(char *const *cmdArgs,
                  const ChildParams &params)
{
    pid_t child_pid = 0;

    args = cmdArgs;

    initComm();

    child_res = 127;
    child_pid = fork();
    if (child_pid == -1)
        return -1;
    else if (child_pid == 0) {
        close(toparent[0]);
        close(tochild[1]);
        unshare(CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS);

        signal_parent('U');
        pid_t p = fork();
        if (p == -1) {
            cerr << "fork (in child) failed." << endl;
            exit(127);
        } else if (p == 0)
            childFunc(params);
        else {
            exit(wait_for_pid(p));
        }
    }
    close(toparent[1]);
    close(tochild[0]);
    wait_child('U');

    uid_t euid = geteuid();
    gid_t egid = getegid();
    int err = 0;
    if (params.uidMappings.size() == 1 && params.uidMappings[0].count == 1 &&
            params.gidMappings.size() == 1 && params.gidMappings[0].count == 1 &&
            params.uidMappings[0].outerID == euid && params.gidMappings[0].outerID == egid) {
        err = uidmap_single(child_pid, params.uidMappings[0].innerID,
                params.gidMappings[0].innerID);
    } else {
        err = uidmap_sub(child_pid, params);
    }

    if (err != 0) {
        kill(child_pid, SIGTERM);
        return -1;
    }

    return child_pid;
}

void erlent::run_child() {
    signal_child('I');
}

// return exit status of pid p
int erlent::wait_for_pid(pid_t p) {
    int status;
    while (waitpid(p, &status, 0) == -1) {
    }
    int ex;
    if (WIFEXITED(status))
        ex = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        ex = 128 + WTERMSIG(status);
    else
        ex = 255;
    return ex;
}

bool ChildParams::addBind(const string &str, vector<pair<string,string>> &binds) {
    size_t pos = str.find(':');
    if (pos == 0 || pos == string::npos)
        return false;
    binds.push_back(make_pair(str.substr(0, pos), str.substr(pos+1)));
    return true;
}

bool ChildParams::addMapping(const string &str, std::vector<Mapping> &mappings)
{
    long innerID, outerID, count;
    char ch;
    istringstream iss(str);

    iss >> innerID;
    iss >> ch;
    if (ch != ':')
        return false;
    iss >> outerID;
    iss >> ch;
    if (ch != ':')
        return false;
    iss >> count;
    if (!iss.eof() || iss.fail())
        return false;

    mappings.push_back(Mapping(innerID, outerID, count));
    return true;
}

unsigned long ChildParams::lookupID(unsigned long inner, const std::vector<Mapping> &mapping)
{
    for (auto it=mapping.cbegin(); it!=mapping.cend(); ++it) {
        if (it->innerID <= inner && inner < it->innerID+it->count)
            return inner - it->innerID + it->outerID;
    }
    return 65534; // nobody / nogroup
}

unsigned long ChildParams::inverseLookupID(unsigned long outer, const std::vector<Mapping> &mapping)
{
    for (auto it=mapping.cbegin(); it!=mapping.cend(); ++it) {
        if (it->outerID <= outer && outer < it->outerID+it->count)
            return outer - it->outerID + it->innerID;
    }
    return 65534; // nobody / nogroup
}
