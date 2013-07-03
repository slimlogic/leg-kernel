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

#ifdef CONFIG_HOTPLUG_CPU
int __ref arch_register_cpu(int cpu)
{
	struct cpuinfo_arm *cpuinfo = &per_cpu(cpu_data, cpu);

	/* BSP cann't be taken down on arm */
	if (cpu)
		cpuinfo->cpu.hotpluggable = 1;

	return register_cpu(&cpuinfo->cpu, cpu);
}
EXPORT_SYMBOL(arch_register_cpu);

void arch_unregister_cpu(int cpu)
{
	unregister_cpu(&per_cpu(cpu_data, cpu).cpu);
}
EXPORT_SYMBOL(arch_unregister_cpu);
#else /* CONFIG_HOTPLUG_CPU */

static int __init arch_register_cpu(int cpu)
{
	return register_cpu(&per_cpu(cpu_data, cpu).cpu, cpu);
}
#endif /* CONFIG_HOTPLUG_CPU */

