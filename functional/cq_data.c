/*
 * Copyright (c) 2013-2015 Intel Corporation.  All rights reserved.
 * Copyright (c) 2016, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under the BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <pip.h>

#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>

#include <shared.h>


int rank, ntasks;
pip_barrier_t gbarrier;

int run_test()
{
	int ret;
	size_t size = 1000;
	struct fi_cq_data_entry comp;

	if (opts.dst_addr) {
		fprintf(stdout,
			"Posting send with CQ data: 0x%" PRIx64 "\n",
			remote_cq_data);
		ret = ft_post_tx(ep, remote_fi_addr, size, remote_cq_data, &tx_ctx);
		if (ret)
			return ret;

		ret = ft_get_tx_comp(tx_seq);
		fprintf(stdout, "Done\n");
	} else {
		fprintf(stdout, "Waiting for CQ data from client\n");
		ret = fi_cq_sread(rxcq, &comp, 1, NULL, -1);
		if (ret < 0) {
			if (ret == -FI_EAVAIL) {
				ret = ft_cq_readerr(rxcq);
			} else {
				FT_PRINTERR("fi_cq_sread", ret);
			}
			return ret;
		}

		if (comp.flags & FI_REMOTE_CQ_DATA) {
			if (comp.data == remote_cq_data) {
				fprintf(stdout, "remote_cq_data: success\n");
				ret = 0;
			} else {
				fprintf(stdout, "error, Expected data:0x%" PRIx64
					", Received data:0x%" PRIx64 "\n",
					remote_cq_data, comp.data);
				ret = -FI_EIO;
			}
		} else {
			fprintf(stdout, "error, CQ data flag not set\n");
			ret = -FI_EBADFLAGS;
		}
	}

	return ret;
}

static int run(void)
{
	int ret;

	if (hints->ep_attr->type == FI_EP_MSG)
		ret = ft_init_fabric_cm();
	else
		ret = ft_init_fabric();
	if (ret)
		return ret;
	
	if(ntasks == 1)
		ret = run_test();
	else
		pip_barrier_wait(&gbarrier);

	pip_barrier_wait(&gbarrier);
	fi_shutdown(ep, 0);
	return ret;
}


int main(int argc, char **argv)
{
	int op, ret = 0;
	pip_barrier_t *lbarrier;
	int (*help_run)(void);

	pip_init( &rank, &ntasks, NULL, 0 );
	pip_barrier_init(&gbarrier, ntasks);
	pip_get_addr(0, "gbarrier", (void**) &lbarrier);
	pip_get_addr(0, "run_test", (void**) &help_run);
	printf("rank %d - pid %d, run_test %p, help_run %p\n", rank, getpid(), run_test, help_run);
	fflush(stdout);
	sleep(8);

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SIZE;
	opts.comp_method = FT_COMP_SREAD;

	hints = fi_allocinfo();
	if (!hints)
			return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "h" ADDR_OPTS INFO_OPTS)) != -1) {
			switch (op) {
					default:
							ft_parse_addr_opts(op, optarg, &opts);
							ft_parseinfo(op, optarg, hints);
							break;
					case '?':
					case 'h':
							ft_usage(argv[0], "A client-server example that transfers CQ data.\n");
							return EXIT_FAILURE;
			}
	}

	if (optind < argc)
			opts.dst_addr = argv[optind];

	hints->domain_attr->cq_data_size = 64;  /* required minimum */
	hints->mode |= FI_CONTEXT | FI_RX_CQ_DATA;

	hints->caps = FI_MSG;
	hints->domain_attr->mr_mode = FI_MR_LOCAL | OFI_MR_BASIC_MAP;

	cq_attr.format = FI_CQ_FORMAT_DATA;

	if(rank == 0)
		ret = run();
	else{
		pip_barrier_wait(lbarrier);
		ret = help_run();
		pip_barrier_wait(lbarrier);
	}

		
	

	ft_free_res();
	return ft_exit_code(ret);
}
