///
/// \file LMS7002M_cgen.c
///
/// Clock generation for the LMS7002M C driver.
///
/// \copyright
/// Copyright (c) 2014-2015 Fairwaves, Inc.
/// Copyright (c) 2014-2015 Rice University
/// SPDX-License-Identifier: Apache-2.0
/// http://www.apache.org/licenses/LICENSE-2.0
///

#include <stdlib.h>
#include "LMS7002M_impl.h"
#include <LMS7002M/LMS7002M_time.h>
#include <LMS7002M/LMS7002M_logger.h>

static long long cgen_cmp_sleep_ticks(void)
{
    return (50*LMS7_time_tps())/1000000; //50 us -> ticks
}

int LMS7002M_set_data_clock(LMS7002M_t *self, const double fref, const double fout, double *factual)
{
    LMS7_logf(LMS7_INFO, "CGEN tune %f MHz begin", fout/1e6);

    //always use the channel A shadow, CGEN is in global register space
    LMS7002M_set_mac_ch(self, LMS_CHA);

    //The equations:
    // fref * N = fvco
    // fvco / fdiv = fout
    // fref * N = fout * fdiv
    int fdiv = 512+2;
    double Ndiv = 0;
    double fvco = 0;

    //calculation loop to find dividers that are possible
    while (true)
    {
        //try the next even divider
        fdiv -= 2;

        Ndiv = fout*fdiv/fref;
        fvco = fout*fdiv;
        LMS7_logf(LMS7_TRACE, "Trying: fdiv = %d, Ndiv = %f, fvco = %f MHz", fdiv, Ndiv, fvco/1e6);

        //check dividers and vco in range...
        if (fdiv < 2) return -1;
        if (fdiv > 512) continue;
        if (Ndiv < 4) return -1;
        if (Ndiv > 512) continue;

        //check vco boundaries
        if (fvco < LMS7002M_CGEN_VCO_LO) continue;
        if (fvco > LMS7002M_CGEN_VCO_HI) continue;

        break; //its good
    }
    LMS7_logf(LMS7_DEBUG, "Using: fdiv = %d, Ndiv = %f, fvco = %f MHz", fdiv, Ndiv, fvco/1e6);

    //stash the freq now that we know the loop above passed
    self->cgen_freq = fout;
    self->cgen_fref = fref;

    //reset
    self->regs->reg_0x0086_reset_n_cgen = 0;
    LMS7002M_regs_spi_write(self, 0x0086);
    self->regs->reg_0x0086_reset_n_cgen = 1;
    LMS7002M_regs_spi_write(self, 0x0086);

    //configure and enable synthesizer
    self->regs->reg_0x0086_en_intonly_sdm_cgen = 0; //support frac-N
    self->regs->reg_0x0086_en_sdm_clk_cgen = 1; //enable
    self->regs->reg_0x0086_pd_cp_cgen = 0; //enable
    self->regs->reg_0x0086_pd_fdiv_fb_cgen = 0; //enable
    self->regs->reg_0x0086_pd_fdiv_o_cgen = 0; //enable
    self->regs->reg_0x0086_pd_sdm_cgen = 0; //enable
    self->regs->reg_0x0086_pd_vco_cgen = 0; //enable
    self->regs->reg_0x0086_pd_vco_comp_cgen = 0; //enable
    self->regs->reg_0x0086_en_g_cgen = 1;
    self->regs->reg_0x0086_en_coarse_cklgen = 0;
    self->regs->reg_0x008b_coarse_start_cgen = 0;
    self->regs->reg_0x0086_spdup_vco_cgen = 1; //fast settling
    LMS7002M_regs_spi_write(self, 0x0086);

    //program the N divider
    const int Nint = (int)Ndiv;
    const int Nfrac = (int)((Ndiv-Nint)*(1 << 20));
    self->regs->reg_0x0087_frac_sdm_cgen = (Nfrac) & 0xffff; //lower 16 bits
    self->regs->reg_0x0088_frac_sdm_cgen = (Nfrac) >> 16; //upper 4 bits
    self->regs->reg_0x0088_int_sdm_cgen = Nint-1;
    LMS7_logf(LMS7_DEBUG, "fdiv = %d, Ndiv = %f, Nint = %d, Nfrac = %d, fvco = %f MHz", fdiv, Ndiv, Nint, Nfrac, fvco/1e6);
    LMS7002M_regs_spi_write(self, 0x0087);
    LMS7002M_regs_spi_write(self, 0x0088);

    //program the feedback divider
    self->regs->reg_0x0089_sel_sdmclk_cgen = REG_0X0089_SEL_SDMCLK_CGEN_CLK_DIV;
    self->regs->reg_0x0089_div_outch_cgen = (fdiv/2)-1;
    LMS7002M_regs_spi_write(self, 0x0089);

    //select the correct CSW for this VCO frequency
    int csw_lowest = -1;
    self->regs->reg_0x008b_csw_vco_cgen = 0;
    for (int i = 7; i >= 0; i--)
    {
        self->regs->reg_0x008b_csw_vco_cgen |= 1 << i;
        LMS7002M_regs_spi_write(self, 0x008B);
        LMS7_sleep_for(cgen_cmp_sleep_ticks());
        LMS7002M_regs_spi_read(self, 0x008C);

        LMS7_logf(LMS7_DEBUG, "i=%d, hi=%d, lo=%d", i, self->regs->reg_0x008c_vco_cmpho_cgen, self->regs->reg_0x008c_vco_cmplo_cgen);
        if (self->regs->reg_0x008c_vco_cmplo_cgen != 0)
        {
            self->regs->reg_0x008b_csw_vco_cgen &= ~(1 << i); //clear bit i
        }
        if (self->regs->reg_0x008c_vco_cmpho_cgen != 0 && self->regs->reg_0x008c_vco_cmplo_cgen == 0 && csw_lowest < 0)
        {
            csw_lowest = self->regs->reg_0x008b_csw_vco_cgen;
        }
        LMS7002M_regs_spi_write(self, 0x008B);
    }

    //find the midpoint for the high and low bounds
    if (csw_lowest >= 0)
    {
        int csw_highest = self->regs->reg_0x008b_csw_vco_cgen;
        if (csw_lowest == csw_highest)
        {
            while (csw_lowest >= 0)
            {
                self->regs->reg_0x008b_csw_vco_cgen = csw_lowest;
                LMS7002M_regs_spi_write(self, 0x008B);
                LMS7_sleep_for(cgen_cmp_sleep_ticks());
                LMS7002M_regs_spi_read(self, 0x008C);

                if (self->regs->reg_0x008c_vco_cmpho_cgen == 0 && self->regs->reg_0x008c_vco_cmplo_cgen == 0) break;
                else csw_lowest--;
            }
            if (csw_lowest < 0) csw_lowest = 0;
        }
        csw_lowest += 1;

        LMS7_logf(LMS7_INFO, "lowest CSW_VCO %i, highest CSW_VCO %i", csw_lowest, csw_highest);
        self->regs->reg_0x008b_csw_vco_cgen = (csw_highest+csw_lowest)/2;
        LMS7002M_regs_spi_write(self, 0x008B);
    }

    //check that the vco selection was successful
    LMS7_sleep_for(cgen_cmp_sleep_ticks());
    LMS7002M_regs_spi_read(self, 0x008C);
    if (self->regs->reg_0x008c_vco_cmpho_cgen != 0 && self->regs->reg_0x008c_vco_cmplo_cgen == 0)
    {
        LMS7_log(LMS7_INFO, "CGEN VCO OK");
    }
    else
    {
        LMS7_log(LMS7_ERROR, "CGEN VCO select FAIL");
        return -3;
    }

    self->regs->reg_0x0086_spdup_vco_cgen = 0; //done with fast settling
    LMS7002M_regs_spi_write(self, 0x0086);

    //calculate the actual rate
    if (factual != NULL) *factual = fref * (Nint + (Nfrac/((double)(1 << 20)))) / fdiv;

    return 0; //OK
}
