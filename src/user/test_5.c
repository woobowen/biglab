/*
    测试五：多进程文件共享与引用计数
    1. 测试 fork 后父子进程是否共享文件偏移量
    2. 测试 file->ref 引用计数是否正确
*/
#include "help.h"

void main(int argc, char *argv[])
{
    uint32 fd;
    uint32 pid;
    char buf[128];
    uint32 len;
    file_stat_t stat;

    fprintf(STDOUT, "Test 5: Process file sharing start...\n");

    // 1. 打开一个文件用于共享写入
    fd = sys_open("shared.txt", OPEN_READ | OPEN_WRITE | OPEN_CREATE);
    if (fd == -1) {
        fprintf(STDERR, "open shared.txt fail\n");
        sys_exit(1);
    }

    // 2. Fork 创建子进程
    pid = sys_fork();

    if (pid == -1) {
        fprintf(STDERR, "fork fail\n");
        sys_exit(1);
    }
    else if (pid == 0) {
        // === 子进程 ===
        fprintf(STDOUT, "[Child] inherit fd %d\n", fd);
        
        // 写入数据，这应该会推进共享的 offset
        char msg_child[] = "Child wrote this. ";
        sys_write(fd, strlen(msg_child), msg_child);
        
        fprintf(STDOUT, "[Child] write done, exit.\n");
        // 子进程退出，会自动关闭 fd。
        // 如果引用计数逻辑错误，这里可能会导致文件被彻底释放，父进程后续操作会崩。
        sys_exit(0);
    }
    else {
        // === 父进程 ===
        uint32 exit_state;
        sys_wait(&exit_state); // 等待子进程写入完毕并退出
        
        fprintf(STDOUT, "[Parent] child exited.\n");

        // 检查 offset 是否被子进程修改了
        // 如果 offset 还是 0，说明父子进程没有共享 file_t
        sys_fstat(fd, &stat);
        if (stat.offset == 0) {
            fprintf(STDERR, "[Error] Offset not shared!\n");
            sys_exit(1);
        } else {
            fprintf(STDOUT, "[Parent] current offset is %d (expected > 0)\n", stat.offset);
        }

        // 父进程继续写入
        char msg_parent[] = "Parent wrote this.\n";
        sys_write(fd, strlen(msg_parent), msg_parent);

        // 关闭文件
        sys_close(fd);
    }

    // 3. 验证文件内容
    // 重新打开文件，读取全部内容，检查是否包含两部分数据
    fd = sys_open("shared.txt", OPEN_READ);
    if (fd == -1) {
        fprintf(STDERR, "re-open shared.txt fail\n");
        sys_exit(1);
    }

    memset(buf, 0, sizeof(buf));
    len = sys_read(fd, sizeof(buf) - 1, buf);
    buf[len] = '\0';

    fprintf(STDOUT, "\nFile Content:\n%s\n", buf);

    sys_close(fd);
    sys_unlink("shared.txt"); // 清理环境

    fprintf(STDOUT, "Test 5 passed!\n");
    sys_exit(0);
}