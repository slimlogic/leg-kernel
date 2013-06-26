#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/cputype.h>
#include <asm/topology.h>
#include <asm/cpu.h>

void arch_fix_phys_package_id(int num, u32 slot)
{
}
EXPORT_SYMBOL_GPL(arch_fix_phys_package_id);

