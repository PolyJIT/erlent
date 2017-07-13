extern "C" {
#include <fcntl.h>
#include <grp.h>
#include <pty.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
}

#include "erlent/child.hh"
#include "erlent/erlent.hh"
#include "erlent/signalrelay.hh"

#include <sstream>

using namespace std;
using namespace erlent;

// create a semaphore shared with child processes
static sem_t *create_semaphore(int initial_value) {
    sem_t *s;
    s = (sem_t *) mmap(NULL, sizeof(*s), PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == NULL) {
        cerr << "Could not create semaphore" << endl;
        exit(1);
    }

    sem_init(s, 1 /*shared*/, initial_value);
    return s;
}

int tochild[2], toparent[2];

static void signal_child(char c) {
    if (write(tochild[1], &c, 1) == -1)
        perror("signal_child");
}

static void signal_parent(char c) {
    if (write(toparent[1], &c, 1) == -1)
        perror("signal_parent");
}

static int wait_child(char expected) {
    char c;
    ssize_t res;
    res = read(toparent[0], &c, 1);
    if (res == -1)
        perror("wait_child");
    else if (res == 0) {
        cerr << "Communication error in wait_child: child gone" << endl;
        return -1;
    } else if (c != expected) {
        cerr << "Communication error in wait_child: expected '" << expected
             << "', got '" << c << "'." << endl;
        return -1;
    }
    return 0;
}

static void wait_parent(char expected) {
    char c;
    ssize_t res;
    res = read(tochild[0], &c, 1);
    if (res == -1)
        perror("wait_parent");
    else if (res == 0) {
        // Parent closed the communication channel, so exit gracefully.
        exit(0);
    } else if (c != expected) {
        cerr << "Communication error in wait_parent: expected '" << expected
             << "', got '" << c << "'."  << endl;
        exit(1);
    }
}


static void mnt(const char *src, const char *dst, const char *fstype, int flags=0,
         const char *options=NULL, ErrorCode retcode=ErrorCode::MNT_FAILED) {
    int res;
    // The mount of /proc sometimes fails with EINVAL, so retry
    // the mount in case of EINVAL after a small delay.
    for (int i=0; i<3; ++i) {
        res = mount(src, dst, fstype, flags, options);
        if (res == -1 && errno == EINVAL)
            usleep(50000);
        else
            break;
    }
    if (res == -1) {
        int err = errno;
        cerr << "Mount of '" << src << "' at '" << dst;
        if (fstype)
            cerr << "' (type " << fstype << ")";
        cerr << " failed: " << strerror(err) << endl;
        exit(retcode);
    }
}

static void exec_child(char *const argv[]) __attribute__ ((noreturn));

// Run the child process (after resetting signal handling)
static void exec_child(char *const args[]) {
    // Reset signal handlers and unblock them
    // in case they are blocked (e.g., by the
    // FUSE parent process).
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGHUP);
    sigaddset(&sigs, SIGQUIT);
    sigprocmask(SIG_UNBLOCK, &sigs, NULL);
    execvp(args[0], args);
    int err = errno;
    cerr << "Could not execute '" << args[0] << "': " << strerror(err) << endl;
    exit(127);
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

// The pid wait_for_pid is waiting for
static int pid_to_wait_for = -1;

// Handler for SIGCHLD in child processs
static void sigchld_action(int signum, siginfo_t *si, void *ctx)
{
    int status;
    FORK_DEBUG {
        cerr << "sigchld_action in process " << getpid()
             << " for child " << si->si_pid << endl;
    }

    if (si->si_pid != pid_to_wait_for)
        waitpid(si->si_pid, &status, 0);
}

static char *const *args;
extern int pivot_root(const char * new_root,const char * put_old);

static char *newroot;

// childFunc MUST use fork in any case,
// either using forkpty() or ordinary fork(); otherwise,
// unsharing the PID namespace does not work.
// The function can return the exit code or call exit().
static int childFunc(const ChildParams &params)
{
    wait_parent('I');

    string root(newroot);

    // cerr << "newroot = " << params.newRoot << endl;
    dbg() << "newwd = " << params.newWorkDir << endl;
    dbg() << "command = ";
    for (char *const *p=args; *p; ++p) {
        dbg() << " " << *p;
    }
    dbg() << endl;

    // make newRoot a mount point so we can use pivot_root later
//    mnt(params.newRoot.c_str(), params.newRoot.c_str(), MS_BIND | MS_REC);
    if (params.devprocsys) {
        mnt("/dev", (root + "/dev").c_str(), nullptr, MS_BIND | MS_REC, nullptr,
            MNT_DEV_FAILED);
        mnt("/sys", (root + "/sys").c_str(), nullptr, MS_BIND | MS_REC, nullptr,
            MNT_SYS_FAILED);
    }
    for (const pair<string,string> &b : params.bindMounts) {
        mnt(b.first.c_str(), (root + "/" + b.second).c_str(), nullptr,
            MS_BIND | MS_REC, nullptr, MNT_FAILED);
    }

    if (chroot(root.c_str()) == -1) {
        int err = errno;
        cerr << "chroot failed: " << strerror(err) << endl;
        exit(1);
    }

    signal_parent('C');

    if (params.devprocsys) {
        // proc can only be mounted after we fork (due to the change in
        // the view to the process IDs, so we mount it below
        gid_t ttygid = 5;
        if (params.existsGidMapping(ttygid)) {
            string ptsopt = "newinstance,ptmxmode=0666,gid=" + to_string(ttygid);
            mnt("erlentpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
                ptsopt.c_str(), MNT_PTS_FAILED);
            mnt("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND);
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
    if (setregid(params.initialGID, params.initialGID) == -1) {
        int err = errno;
        cerr << "setregid failed: " << strerror(err) << endl;
        exit(1);
    }
    setgroups(0, NULL);

    // Make sure the signal handler for
    // SIGCHLD is _not_ SIG_IGN because in this case
    // waitpid can fail with ECHLD (there is no child
    // any more to wait for). To avoid zombie/defunct
    // processes, we need to handle SIGCHLD signals.
    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_sigaction = sigchld_action;
    sact.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
    if (sigaction(SIGCHLD, &sact, 0) == -1) {
        perror("sigaction");
        exit(127);
    }

    bool ptyEmu = isatty(0) && params.devprocsys;  // need /dev/pts
    struct termios oldsettings;
    struct winsize ws;

    if (ptyEmu) {
        if (tcgetattr(0, &oldsettings) == -1) {
            int err = errno;
            cerr << "tcgetattr failed: " << strerror(err) << endl;
            exit(127);
        }
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, (char *) &ws) < 0) {
            cerr << "TIOCGWINSZ error" << endl;
            exit(127);
        }
    }

    sem_t *sema = create_semaphore(0);
    pid_t p;
    if (ptyEmu)
        p = forkpty(&amaster, NULL, &oldsettings, &ws);
    else
        p = fork();

    if (p == -1) {
        int err = errno;
        cerr << "fork[pty] failed: " << strerror(err) << endl;
        exit(127);
    } else if (p == 0) {
        if (params.devprocsys)
            mnt("proc", "/proc", "proc", 0, nullptr, MNT_PROC_FAILED);
        sem_wait(sema);
        exec_child(args);  // never returns
    }

    // Do not consume the exit status of this process
    // in the signal handler for SIGCHLD;
    pid_to_wait_for = p;

    // Forward some signals to child
    install_signal_relay({p}, { SIGTERM, SIGINT, SIGHUP, SIGQUIT });

    if (ptyEmu) {
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
    }

    sem_post(sema); // run child

    if (ptyEmu) {
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
    }

    int res = wait_for_pid(p, {p});
    FORK_DEBUG {
        cerr << "child helper (" << getpid() << ") got exit status "
             << res << "from child (" << p << ")" << endl;
    }

    return res;
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
        // when the "setgroups" file does not exist
        // (because we are on an old kernel),
        // do not complain.
        if (errno != ENOENT) {
            perror("open /proc/.../setgroups");
            return -1;
        }
    } else {
        sprintf(str, "deny");
        if (write(fd, str, strlen(str)) == -1) {
            perror("write setgroups");
            return -1;
        }
        close(fd);
    }

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
    size_t n_mappings = mappings.size();
    char **argv = new char* [3*n_mappings+3];
    int argc = 0;
    argv[argc++] = strdup(cmd);
    argv[argc++] = strdup(to_string(child_pid).c_str());
    for (const Mapping &m : mappings) {
        argv[argc++] = strdup(to_string(m.innerID).c_str());
        argv[argc++] = strdup(to_string(m.outerID).c_str());
        argv[argc++] = strdup(to_string(m.count).c_str());
    }
    argv[argc] = NULL;
    int res = -1;
    int pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);
        exit(127);
    } else if (pid > 0) {
        res = wait_for_pid(pid, {pid});
    } else if (pid < 0) {
        cerr << "fork failed" << endl;
        res = 127;
    }

    if (res != 0) {
        for (int i=0; i<argc; ++i)
            cerr << "" "" << argv[i];
        cerr << ": cannot invoke, exit code " << res << endl;
        res = -1;
    }

    for (int i=0; i<argc; ++i)
        free(argv[i]);
    delete[] argv;

    return res;
}

int uidmap_sub(pid_t child_pid, const ChildParams &params) {
    if (do_idmap("/usr/bin/newuidmap", child_pid, params.uidMappings) != 0)
        return -1;
    if (do_idmap("/usr/bin/newgidmap", child_pid, params.gidMappings) != 0)
        return -1;
    return 0;
}

pid_t erlent::setup_child(char *const *cmdArgs,
                          ChildParams params)
{
    pid_t child_pid = 0;

    args = cmdArgs;

    newroot = (char *) mmap(NULL, sizeof(*newroot)*256, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    initComm();

    child_pid = fork();
    if (child_pid == -1)
        return -1;
    else if (child_pid == 0) {
        close(toparent[0]);
        close(tochild[1]);
        int flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS;
        if (params.unshareNet)
            flags |= CLONE_NEWNET;
        int res = unshare(flags);
        if (res == -1) {
            int err = errno;
            cerr << "unshare failed: " << strerror(err) << endl;
            exit(127);
        }

        signal_parent('U');
        exit(childFunc(params));
    }
    close(toparent[1]);
    close(tochild[0]);

    int err = 0;

    err = wait_child('U');
    if (err == 0) {
        uid_t euid = geteuid();
        gid_t egid = getegid();
        if (params.uidMappings.size() == 1 && params.uidMappings[0].count == 1 &&
                params.gidMappings.size() == 1 && params.gidMappings[0].count == 1 &&
                params.uidMappings[0].outerID == euid && params.gidMappings[0].outerID == egid) {
            err = uidmap_single(child_pid, params.uidMappings[0].innerID,
                    params.gidMappings[0].innerID);
        } else {
            err = uidmap_sub(child_pid, params);
        }
    }

    if (err != 0) {
        // There was an error setting up the child environment,
        // kill the child process.
        kill(child_pid, SIGKILL);
    }

    return child_pid;
}

void erlent::run_child(const string &newRoot) {
    strncpy(newroot, newRoot.c_str(), 255);
    newroot[256] = '\0';
    signal_child('I');
}

// Wait until the child has performed chroot()
void erlent::wait_child_chroot() {
    wait_child('C');
}

// Try to remove the temporary directory early.
// Since the child has a separate mount namespace,
// we can unmount the fuse filesystem in the parent
// (at least when /dev is mounted in the child; it is
// unclear why it only works with a bind mount for /dev)
// and remove the temporary directory.
void erlent::parent_fuse_preclean() {
    char *const args[] = {
        strdup("/bin/fusermount"), strdup("-u"),
        strdup("-q"), newroot, NULL };
    int res = 0;
    pid_t pid = fork();
    if (pid == 0) {
        execv(args[0], args);
        exit(127);
    } else
        res = wait_for_pid(pid, {});
    if (res == 0) {
        if (rmdir(newroot) == -1) {
            int err = errno;
            dbg() << "Cound not remove temporary directory '"
                  << newroot << "': " << strerror(err) << endl;
        }
    } else
        dbg() << "Could not unmount fuse filesystem: " << res << endl;
}

// Wait for process with PID 'p' to exit. While waiting,
// forward termination signals to the PIDs in 'forward_to'.
int erlent::wait_for_pid(pid_t p, const initializer_list<pid_t> &forward_to) {
    int status;

    pid_to_wait_for = p;

    // Forward some signals to child
    install_signal_relay(forward_to, { SIGTERM, SIGINT, SIGHUP, SIGQUIT });

    FORK_DEBUG { cerr << "process " << getpid() << " waiting for pid " << p << endl; }
    while (waitpid(p, &status, 0) == -1) {
        int err = errno;
        switch (err) {
        case EINTR: continue;
        case ECHILD:
            FORK_DEBUG {
                cerr << "in process " << getpid() << ": child " << p << " has vanished" << endl;
            }
            return 0;
        default:
            cerr << "waitpid failed: " << strerror(err) << endl;
            return 255;
        }
    }
    int ex;
    if (WIFEXITED(status))
        ex = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        ex = 128 + WTERMSIG(status);
    else
        ex = 255;
    FORK_DEBUG { cerr << "process " << getpid() << ": child " << p << " exited with " << ex << endl; }
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

bool ChildParams::existsGidMapping(gid_t inner) const
{
    for (const Mapping &m : gidMappings) {
        if (m.innerID <= inner && inner < m.innerID+m.count)
            return true;
    }
    return false;
}
