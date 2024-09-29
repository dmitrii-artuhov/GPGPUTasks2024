#include "libgpu/shared_device_buffer.h"


#include <libutils/fast_random.h>
#include <libutils/misc.h>
#include <libutils/timer.h>

#include "cl/sum_cl.h"
#include "libgpu/context.h"

template<typename T>
void raiseFail(const T &a, const T &b, std::string message, std::string filename, int line)
{
    if (a != b) {
        std::cerr << message << " But " << a << " != " << b << ", " << filename << ":" << line << std::endl;
        throw std::runtime_error(message);
    }
}

#define EXPECT_THE_SAME(a, b, message) raiseFail(a, b, message, __FILE__, __LINE__)




void runGpuKernel(
    int benchmarkingIters,
    const std::string& kernel_name,
    const gpu::WorkSize& work_size,
    gpu::gpu_mem_32u& as_gpu,
    gpu::gpu_mem_32u& sum_gpu,
    unsigned int n,
    unsigned int reference_sum,
    bool printLog = false
) {
    ocl::Kernel kernel(sum_kernel, sum_kernel_length, kernel_name);
    kernel.compile(printLog);

    timer t;
    for (int iter = 0; iter < benchmarkingIters; ++iter) {
        unsigned int sum = 0;
        sum_gpu.writeN(&sum, 1);

        kernel.exec(
            work_size,
            as_gpu,
            sum_gpu,
            n
        );

        sum_gpu.readN(&sum, 1);
        EXPECT_THE_SAME(reference_sum, sum, "GPU result should be consistent!");
        t.nextLap();
    }
    std::cout << "GPU <" << kernel_name << ">: " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
    std::cout << "GPU <" << kernel_name << ">: " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;

    std::cout << std::endl;
}

int main(int argc, char **argv)
{
    try {
        int benchmarkingIters = 10;

        unsigned int reference_sum = 0;
        unsigned int n = 100*1000*1000;
        std::vector<unsigned int> as(n, 0);
        FastRandom r(42);
        for (int i = 0; i < n; ++i) {
            as[i] = (unsigned int) r.next(0, std::numeric_limits<unsigned int>::max() / n);
            reference_sum += as[i];
        }

        // Pure CPU
        {
            timer t;
            for (int iter = 0; iter < benchmarkingIters; ++iter) {
                unsigned int sum = 0;
                for (int i = 0; i < n; ++i) {
                    sum += as[i];
                }
                EXPECT_THE_SAME(reference_sum, sum, "CPU result should be consistent!");
                t.nextLap();
            }
            std::cout << "CPU:     " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
            std::cout << "CPU:     " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;

            std::cout << std::endl;
        }

        // CPU with OpenMP
        {
            timer t;
            for (int iter = 0; iter < benchmarkingIters; ++iter) {
                unsigned int sum = 0;
                #pragma omp parallel for reduction(+:sum)
                for (int i = 0; i < n; ++i) {
                    sum += as[i];
                }
                EXPECT_THE_SAME(reference_sum, sum, "CPU OpenMP result should be consistent!");
                t.nextLap();
            }
            std::cout << "CPU OMP: " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
            std::cout << "CPU OMP: " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;

            std::cout << std::endl;
        }

        {
            // TODO: implement on OpenCL
            gpu::Device device = gpu::chooseGPUDevice(argc, argv);

            gpu::Context context;
            context.init(device.device_id_opencl);
            context.activate();

            gpu::gpu_mem_32u as_gpu;
            gpu::gpu_mem_32u sum_gpu;

            as_gpu.resizeN(n);
            sum_gpu.resizeN(1);

            as_gpu.writeN(as.data(), n);

            // GPU with global atomic
            runGpuKernel(benchmarkingIters, "sum_gpu_global_atomic", gpu::WorkSize(128, n), as_gpu, sum_gpu, n, reference_sum);

            // // GPU with cycle
            runGpuKernel(benchmarkingIters, "sum_gpu_cycle", gpu::WorkSize(128, n / 64), as_gpu, sum_gpu, n, reference_sum);

            // GPU with coalesed cycle
            runGpuKernel(benchmarkingIters, "sum_gpu_coalesed_cycle", gpu::WorkSize(128, n / 64), as_gpu, sum_gpu, n, reference_sum);

            // GPU with local buffer
            runGpuKernel(benchmarkingIters, "sum_gpu_local", gpu::WorkSize(128, n), as_gpu, sum_gpu, n, reference_sum);

            // GPU with tree
            runGpuKernel(benchmarkingIters, "sum_gpu_tree", gpu::WorkSize(128, n), as_gpu, sum_gpu, n, reference_sum);
        }
    }
    catch(const std::exception& err) {
        std::cerr << err.what() << std::endl;
        throw;
    }
}