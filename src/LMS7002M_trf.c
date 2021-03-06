///
/// \file LMS7002M_trf.c
///
/// TX RF frontend controls for the LMS7002M C driver.
///
/// \copyright
/// Copyright (c) 2015-2015 Fairwaves, Inc.
/// Copyright (c) 2015-2015 Rice University
/// SPDX-License-Identifier: Apache-2.0
/// http://www.apache.org/licenses/LICENSE-2.0
///

#include <stdlib.h>
#include "LMS7002M_impl.h"

void LMS7002M_trf_enable(LMS7002M_t *self, const LMS7002M_chan_t channel, const bool enable)
{
    LMS7002M_set_mac_ch(self, channel);
    self->regs->reg_0x0124_en_dir_trf = 1;
    LMS7002M_regs_spi_write(self, 0x0124);

    self->regs->reg_0x0100_en_g_trf = enable?1:0;
    self->regs->reg_0x0100_pd_tlobuf_trf = 0;
    self->regs->reg_0x0100_pd_txpad_trf = 0;
    LMS7002M_regs_spi_write(self, 0x0100);
}

void LMS7002M_trf_select_band(LMS7002M_t *self, const LMS7002M_chan_t channel, const int band)
{
    LMS7002M_set_mac_ch(self, channel);
    self->regs->reg_0x0103_sel_band1_trf = (band == 1)?1:0;
    self->regs->reg_0x0103_sel_band2_trf = (band == 2)?1:0;
    LMS7002M_regs_spi_write(self, 0x0103);
}

void LMS7002M_trf_enable_loopback(LMS7002M_t *self, const LMS7002M_chan_t channel, const bool enable)
{
    LMS7002M_set_mac_ch(self, channel);
    self->regs->reg_0x0101_en_loopb_txpad_trf = enable?0:1;
    LMS7002M_regs_spi_write(self, 0x0101);
}

double LMS7002M_trf_set_pad(LMS7002M_t *self, const LMS7002M_chan_t channel, const double gain)
{
    const double pmax = 0;
    double loss = pmax-gain;

    //different scaling realm
    if (loss > 10) loss = (loss+10)/2;

    //clip
    if (loss > 31) loss = 31;
    if (loss < 0) loss = 0;

    //integer round
    int loss_int = (int)(loss + 0.5);

    LMS7002M_set_mac_ch(self, channel);
    self->regs->reg_0x0101_loss_lin_txpad_trf = loss_int;
    self->regs->reg_0x0101_loss_main_txpad_trf = loss_int;
    LMS7002M_regs_spi_write(self, 0x0101);

    if (loss_int > 10) return pmax-10-2*(loss_int-10);
    return pmax-loss_int;
}
