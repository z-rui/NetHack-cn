/* NetHack 3.6	mcastu.c	$NHDT-Date: 1436753517 2015/07/13 02:11:57 $  $NHDT-Branch: master $:$NHDT-Revision: 1.44 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2011. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

/* monster mage spells */
enum mcast_mage_spells {
    MGC_PSI_BOLT = 0,
    MGC_CURE_SELF,
    MGC_HASTE_SELF,
    MGC_STUN_YOU,
    MGC_DISAPPEAR,
    MGC_WEAKEN_YOU,
    MGC_DESTRY_ARMR,
    MGC_CURSE_ITEMS,
    MGC_AGGRAVATION,
    MGC_SUMMON_MONS,
    MGC_CLONE_WIZ,
    MGC_DEATH_TOUCH
};

/* monster cleric spells */
enum mcast_cleric_spells {
    CLC_OPEN_WOUNDS = 0,
    CLC_CURE_SELF,
    CLC_CONFUSE_YOU,
    CLC_PARALYZE,
    CLC_BLIND_YOU,
    CLC_INSECTS,
    CLC_CURSE_ITEMS,
    CLC_LIGHTNING,
    CLC_FIRE_PILLAR,
    CLC_GEYSER
};

STATIC_DCL void FDECL(cursetxt, (struct monst *, BOOLEAN_P));
STATIC_DCL int FDECL(choose_magic_spell, (int));
STATIC_DCL int FDECL(choose_clerical_spell, (int));
STATIC_DCL int FDECL(m_cure_self, (struct monst *, int));
STATIC_DCL void FDECL(cast_wizard_spell, (struct monst *, int, int));
STATIC_DCL void FDECL(cast_cleric_spell, (struct monst *, int, int));
STATIC_DCL boolean FDECL(is_undirected_spell, (unsigned int, int));
STATIC_DCL boolean
FDECL(spell_would_be_useless, (struct monst *, unsigned int, int));

extern const char *const flash_types[]; /* from zap.c */

/* feedback when frustrated monster couldn't cast a spell */
STATIC_OVL
void
cursetxt(mtmp, undirected)
struct monst *mtmp;
boolean undirected;
{
    if (canseemon(mtmp) && couldsee(mtmp->mx, mtmp->my)) {
        const char *point_msg; /* spellcasting monsters are impolite */

        if (undirected)
            point_msg = "周围, 然后咒骂";
        else if ((Invis && !perceives(mtmp->data)
                  && (mtmp->mux != u.ux || mtmp->muy != u.uy))
                 || is_obj_mappear(&youmonst, STRANGE_OBJECT)
                 || u.uundetected)
            point_msg = "并咒骂你大致的方向";
        else if (Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy))
            point_msg = "并咒骂你的位移幻影";
        else
            point_msg = "你, 然后咒骂";

        pline("%s 指着%s.", Monnam(mtmp), point_msg);
    } else if ((!(moves % 4) || !rn2(4))) {
        if (!Deaf)
            Norep("你听见嘟哝的咒骂声.");
    }
}

/* convert a level based random selection into a specific mage spell;
   inappropriate choices will be screened out by spell_would_be_useless() */
STATIC_OVL int
choose_magic_spell(spellval)
int spellval;
{
    /* for 3.4.3 and earlier, val greater than 22 selected the default spell
     */
    while (spellval > 24 && rn2(25))
        spellval = rn2(spellval);

    switch (spellval) {
    case 24:
    case 23:
        if (Antimagic || Hallucination)
            return MGC_PSI_BOLT;
        /*FALLTHRU*/
    case 22:
    case 21:
    case 20:
        return MGC_DEATH_TOUCH;
    case 19:
    case 18:
        return MGC_CLONE_WIZ;
    case 17:
    case 16:
    case 15:
        return MGC_SUMMON_MONS;
    case 14:
    case 13:
        return MGC_AGGRAVATION;
    case 12:
    case 11:
    case 10:
        return MGC_CURSE_ITEMS;
    case 9:
    case 8:
        return MGC_DESTRY_ARMR;
    case 7:
    case 6:
        return MGC_WEAKEN_YOU;
    case 5:
    case 4:
        return MGC_DISAPPEAR;
    case 3:
        return MGC_STUN_YOU;
    case 2:
        return MGC_HASTE_SELF;
    case 1:
        return MGC_CURE_SELF;
    case 0:
    default:
        return MGC_PSI_BOLT;
    }
}

/* convert a level based random selection into a specific cleric spell */
STATIC_OVL int
choose_clerical_spell(spellnum)
int spellnum;
{
    /* for 3.4.3 and earlier, num greater than 13 selected the default spell
     */
    while (spellnum > 15 && rn2(16))
        spellnum = rn2(spellnum);

    switch (spellnum) {
    case 15:
    case 14:
        if (rn2(3))
            return CLC_OPEN_WOUNDS;
        /*FALLTHRU*/
    case 13:
        return CLC_GEYSER;
    case 12:
        return CLC_FIRE_PILLAR;
    case 11:
        return CLC_LIGHTNING;
    case 10:
    case 9:
        return CLC_CURSE_ITEMS;
    case 8:
        return CLC_INSECTS;
    case 7:
    case 6:
        return CLC_BLIND_YOU;
    case 5:
    case 4:
        return CLC_PARALYZE;
    case 3:
    case 2:
        return CLC_CONFUSE_YOU;
    case 1:
        return CLC_CURE_SELF;
    case 0:
    default:
        return CLC_OPEN_WOUNDS;
    }
}

/* return values:
 * 1: successful spell
 * 0: unsuccessful spell
 */
int
castmu(mtmp, mattk, thinks_it_foundyou, foundyou)
register struct monst *mtmp;
register struct attack *mattk;
boolean thinks_it_foundyou;
boolean foundyou;
{
    int dmg, ml = mtmp->m_lev;
    int ret;
    int spellnum = 0;

    /* Three cases:
     * -- monster is attacking you.  Search for a useful spell.
     * -- monster thinks it's attacking you.  Search for a useful spell,
     *    without checking for undirected.  If the spell found is directed,
     *    it fails with cursetxt() and loss of mspec_used.
     * -- monster isn't trying to attack.  Select a spell once.  Don't keep
     *    searching; if that spell is not useful (or if it's directed),
     *    return and do something else.
     * Since most spells are directed, this means that a monster that isn't
     * attacking casts spells only a small portion of the time that an
     * attacking monster does.
     */
    if ((mattk->adtyp == AD_SPEL || mattk->adtyp == AD_CLRC) && ml) {
        int cnt = 40;

        do {
            spellnum = rn2(ml);
            if (mattk->adtyp == AD_SPEL)
                spellnum = choose_magic_spell(spellnum);
            else
                spellnum = choose_clerical_spell(spellnum);
            /* not trying to attack?  don't allow directed spells */
            if (!thinks_it_foundyou) {
                if (!is_undirected_spell(mattk->adtyp, spellnum)
                    || spell_would_be_useless(mtmp, mattk->adtyp, spellnum)) {
                    if (foundyou)
                        impossible(
                       "spellcasting monster found you and doesn't know it?");
                    return 0;
                }
                break;
            }
        } while (--cnt > 0
                 && spell_would_be_useless(mtmp, mattk->adtyp, spellnum));
        if (cnt == 0)
            return 0;
    }

    /* monster unable to cast spells? */
    if (mtmp->mcan || mtmp->mspec_used || !ml) {
        cursetxt(mtmp, is_undirected_spell(mattk->adtyp, spellnum));
        return (0);
    }

    if (mattk->adtyp == AD_SPEL || mattk->adtyp == AD_CLRC) {
        mtmp->mspec_used = 10 - mtmp->m_lev;
        if (mtmp->mspec_used < 2)
            mtmp->mspec_used = 2;
    }

    /* monster can cast spells, but is casting a directed spell at the
       wrong place?  If so, give a message, and return.  Do this *after*
       penalizing mspec_used. */
    if (!foundyou && thinks_it_foundyou
        && !is_undirected_spell(mattk->adtyp, spellnum)) {
        pline("%s 向%s施展了一个魔法!",
              canseemon(mtmp) ? Monnam(mtmp) : "什么东西",
              levl[mtmp->mux][mtmp->muy].typ == WATER ? "空水"
                                                      : "稀薄的空气");
        return (0);
    }

    nomul(0);
    if (rn2(ml * 10) < (mtmp->mconf ? 100 : 20)) { /* fumbled attack */
        if (canseemon(mtmp) && !Deaf)
            pline_The("空气在%s周围发出爆裂声.", mon_nam(mtmp));
        return (0);
    }
    if (canspotmon(mtmp) || !is_undirected_spell(mattk->adtyp, spellnum)) {
        pline("%s%s施展了一个魔法!",
              canspotmon(mtmp) ? Monnam(mtmp) : "什么东西",
              is_undirected_spell(mattk->adtyp, spellnum)
                  ? ""
                  : (Invisible && !perceives(mtmp->data)
                     && (mtmp->mux != u.ux || mtmp->muy != u.uy))
                        ? "向你附近的一点"
                        : (Displaced
                           && (mtmp->mux != u.ux || mtmp->muy != u.uy))
                              ? "向你的位移幻影"
                              : "向你");
    }

    /*
     * As these are spells, the damage is related to the level
     * of the monster casting the spell.
     */
    if (!foundyou) {
        dmg = 0;
        if (mattk->adtyp != AD_SPEL && mattk->adtyp != AD_CLRC) {
            impossible(
              "%s casting non-hand-to-hand version of hand-to-hand spell %d?",
                       Monnam(mtmp), mattk->adtyp);
            return (0);
        }
    } else if (mattk->damd)
        dmg = d((int) ((ml / 2) + mattk->damn), (int) mattk->damd);
    else
        dmg = d((int) ((ml / 2) + 1), 6);
    if (Half_spell_damage)
        dmg = (dmg + 1) / 2;

    ret = 1;

    switch (mattk->adtyp) {
    case AD_FIRE:
        pline("你被火焰笼罩.");
        if (Fire_resistance) {
            shieldeff(u.ux, u.uy);
            pline("但你抵抗了其效果.");
            dmg = 0;
        }
        burn_away_slime();
        break;
    case AD_COLD:
        pline("你被冰霜覆盖.");
        if (Cold_resistance) {
            shieldeff(u.ux, u.uy);
            pline("但你抵抗了其效果.");
            dmg = 0;
        }
        break;
    case AD_MAGM:
        You("被弹雨打中了!");
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            pline_The("飞弹弹开了!");
            dmg = 0;
        } else
            dmg = d((int) mtmp->m_lev / 2 + 1, 6);
        break;
    case AD_SPEL: /* wizard spell */
    case AD_CLRC: /* clerical spell */
    {
        if (mattk->adtyp == AD_SPEL)
            cast_wizard_spell(mtmp, dmg, spellnum);
        else
            cast_cleric_spell(mtmp, dmg, spellnum);
        dmg = 0; /* done by the spell casting functions */
        break;
    }
    }
    if (dmg)
        mdamageu(mtmp, dmg);
    return (ret);
}

STATIC_OVL int
m_cure_self(mtmp, dmg)
struct monst *mtmp;
int dmg;
{
    if (mtmp->mhp < mtmp->mhpmax) {
        if (canseemon(mtmp))
            pline("%s看起来好些了.", Monnam(mtmp));
        /* note: player healing does 6d4; this used to do 1d8 */
        if ((mtmp->mhp += d(3, 6)) > mtmp->mhpmax)
            mtmp->mhp = mtmp->mhpmax;
        dmg = 0;
    }
    return dmg;
}

/* monster wizard and cleric spellcasting functions */
/*
   If dmg is zero, then the monster is not casting at you.
   If the monster is intentionally not casting at you, we have previously
   called spell_would_be_useless() and spellnum should always be a valid
   undirected spell.
   If you modify either of these, be sure to change is_undirected_spell()
   and spell_would_be_useless().
 */
STATIC_OVL
void
cast_wizard_spell(mtmp, dmg, spellnum)
struct monst *mtmp;
int dmg;
int spellnum;
{
    if (dmg == 0 && !is_undirected_spell(AD_SPEL, spellnum)) {
        impossible("cast directed wizard spell (%d) with dmg=0?", spellnum);
        return;
    }

    switch (spellnum) {
    case MGC_DEATH_TOUCH:
        pline("噢不, %s在使用死亡之触!", mhe(mtmp));
        if (nonliving(youmonst.data) || is_demon(youmonst.data)) {
            You("看起来没有比以前更加死亡.");
        } else if (!Antimagic && rn2(mtmp->m_lev) > 12) {
            if (Hallucination) {
                You("有一种灵魂出窍的体验.");
            } else {
                killer.format = KILLED_BY_AN;
                Strcpy(killer.name, "死亡之触");
                done(DIED);
            }
        } else {
            if (Antimagic)
                shieldeff(u.ux, u.uy);
            pline("你很幸运, 它没起作用!");
        }
        dmg = 0;
        break;
    case MGC_CLONE_WIZ:
        if (mtmp->iswiz && context.no_of_wizards == 1) {
            pline("双重麻烦...");
            clonewiz();
            dmg = 0;
        } else
            impossible("bad wizard cloning?");
        break;
    case MGC_SUMMON_MONS: {
        int count;

        count = nasty(mtmp); /* summon something nasty */
        if (mtmp->iswiz)
            verbalize("消灭了这个贼, 我的宠物!");
        else {
            const char *mappear =
                (count == 1) ? "一只怪物" : "怪物们";

            /* messages not quite right if plural monsters created but
               only a single monster is seen */
            if (Invisible && !perceives(mtmp->data)
                && (mtmp->mux != u.ux || mtmp->muy != u.uy))
                pline("%s 在你附近的一个地方出现了!", mappear);
            else if (Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy))
                pline("%s 在你的位移幻影周围出现了!", mappear);
            else
                pline("%s 不知道从哪里冒了出来!", mappear);
        }
        dmg = 0;
        break;
    }
    case MGC_AGGRAVATION:
        You_feel("那个怪物意识到了你的存在.");
        aggravate();
        dmg = 0;
        break;
    case MGC_CURSE_ITEMS:
        You_feel("好像你需要一些帮助.");
        rndcurse();
        dmg = 0;
        break;
    case MGC_DESTRY_ARMR:
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            pline("一个力场围绕着你!");
        } else if (!destroy_arm(some_armor(&youmonst))) {
            Your("皮肤发痒.");
        }
        dmg = 0;
        break;
    case MGC_WEAKEN_YOU: /* drain strength */
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            You_feel("暂时地虚弱了.");
        } else {
            You("突然感觉虚弱了!");
            dmg = mtmp->m_lev - 6;
            if (Half_spell_damage)
                dmg = (dmg + 1) / 2;
            losestr(rnd(dmg));
            if (u.uhp < 1)
                done_in_by(mtmp, DIED);
        }
        dmg = 0;
        break;
    case MGC_DISAPPEAR: /* makes self invisible */
        if (!mtmp->minvis && !mtmp->invis_blkd) {
            if (canseemon(mtmp))
                pline("%s 突然%s!", Monnam(mtmp),
                      !See_invisible ? "消失了" : "变得透明了");
            mon_set_minvis(mtmp);
            if (cansee(mtmp->mx, mtmp->my) && !canspotmon(mtmp))
                map_invisible(mtmp->mx, mtmp->my);
            dmg = 0;
        } else
            impossible("no reason for monster to cast disappear spell?");
        break;
    case MGC_STUN_YOU:
        if (Antimagic || Free_action) {
            shieldeff(u.ux, u.uy);
            if (!Stunned)
                You_feel("暂时迷惑的.");
            make_stunned(1L, FALSE);
        } else {
            You(Stunned ? "努力保持你的平衡." : "眩晕...");
            dmg = d(ACURR(A_DEX) < 12 ? 6 : 4, 4);
            if (Half_spell_damage)
                dmg = (dmg + 1) / 2;
            make_stunned((HStun & TIMEOUT) + (long) dmg, FALSE);
        }
        dmg = 0;
        break;
    case MGC_HASTE_SELF:
        mon_adjust_speed(mtmp, 1, (struct obj *) 0);
        dmg = 0;
        break;
    case MGC_CURE_SELF:
        dmg = m_cure_self(mtmp, dmg);
        break;
    case MGC_PSI_BOLT:
        /* prior to 3.4.0 Antimagic was setting the damage to 1--this
           made the spell virtually harmless to players with magic res. */
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            dmg = (dmg + 1) / 2;
        }
        if (dmg <= 5)
            You("有些轻微的%s痛.", body_part(HEAD));
        else if (dmg <= 10)
            Your("大脑着火了!");
        else if (dmg <= 20)
            Your("%s 突然疼得痛苦!", body_part(HEAD));
        else
            Your("%s 突然疼得非常痛苦!", body_part(HEAD));
        break;
    default:
        impossible("mcastu: invalid magic spell (%d)", spellnum);
        dmg = 0;
        break;
    }

    if (dmg)
        mdamageu(mtmp, dmg);
}

STATIC_OVL
void
cast_cleric_spell(mtmp, dmg, spellnum)
struct monst *mtmp;
int dmg;
int spellnum;
{
    if (dmg == 0 && !is_undirected_spell(AD_CLRC, spellnum)) {
        impossible("cast directed cleric spell (%d) with dmg=0?", spellnum);
        return;
    }

    switch (spellnum) {
    case CLC_GEYSER:
        /* this is physical damage (force not heat),
         * not magical damage or fire damage
         */
        pline("一个突然不知从哪儿来的间歇泉猛击了你!");
        dmg = d(8, 6);
        if (Half_physical_damage)
            dmg = (dmg + 1) / 2;
        break;
    case CLC_FIRE_PILLAR:
        pline("一根火焰之柱攻击你的周围!");
        if (Fire_resistance) {
            shieldeff(u.ux, u.uy);
            dmg = 0;
        } else
            dmg = d(8, 6);
        if (Half_spell_damage)
            dmg = (dmg + 1) / 2;
        burn_away_slime();
        (void) burnarmor(&youmonst);
        destroy_item(SCROLL_CLASS, AD_FIRE);
        destroy_item(POTION_CLASS, AD_FIRE);
        destroy_item(SPBOOK_CLASS, AD_FIRE);
        (void) burn_floor_objects(u.ux, u.uy, TRUE, FALSE);
        break;
    case CLC_LIGHTNING: {
        boolean reflects;

        pline("一道闪电从你上面打下来!");
        reflects = ureflects("它从你的%s%s弹开.", "");
        if (reflects || Shock_resistance) {
            shieldeff(u.ux, u.uy);
            dmg = 0;
            if (reflects)
                break;
        } else
            dmg = d(8, 6);
        if (Half_spell_damage)
            dmg = (dmg + 1) / 2;
        destroy_item(WAND_CLASS, AD_ELEC);
        destroy_item(RING_CLASS, AD_ELEC);
        (void) flashburn((long) rnd(100));
        break;
    }
    case CLC_CURSE_ITEMS:
        You_feel("好像你需要一些帮助.");
        rndcurse();
        dmg = 0;
        break;
    case CLC_INSECTS: {
        /* Try for insects, and if there are none
           left, go for (sticks to) snakes.  -3. */
        struct permonst *pm = mkclass(S_ANT, 0);
        struct monst *mtmp2 = (struct monst *) 0;
        char let = (pm ? S_ANT : S_SNAKE);
        boolean success = FALSE, seecaster;
        int i, quan, oldseen, newseen;
        coord bypos;
        const char *fmt;

        oldseen = monster_census(TRUE);
        quan = (mtmp->m_lev < 2) ? 1 : rnd((int) mtmp->m_lev / 2);
        if (quan < 3)
            quan = 3;
        for (i = 0; i <= quan; i++) {
            if (!enexto(&bypos, mtmp->mux, mtmp->muy, mtmp->data))
                break;
            if ((pm = mkclass(let, 0)) != 0
                && (mtmp2 = makemon(pm, bypos.x, bypos.y, MM_ANGRY)) != 0) {
                success = TRUE;
                mtmp2->msleeping = mtmp2->mpeaceful = mtmp2->mtame = 0;
                set_malign(mtmp2);
            }
        }
        newseen = monster_census(TRUE);

        /* not canspotmon(), which includes unseen things sensed via warning
         */
        seecaster = canseemon(mtmp) || tp_sensemon(mtmp) || Detect_monsters;

        fmt = 0;
        if (!seecaster) {
            char *arg; /* [not const: upstart(N==1 ? an() : makeplural())] */
            const char *what = (let == S_SNAKE) ? "蛇" : "昆虫";

            if (newseen <= oldseen || Unaware) {
                /* unseen caster fails or summons unseen critters,
                   or unconscious hero ("You dream that you hear...") */
                You_hear("某人在召唤%s.", makeplural(what));
            } else {
                /* unseen caster summoned seen critter(s) */
                arg = (newseen == oldseen + 1) ? an(what) : makeplural(what);
                if (!Deaf)
                    You_hear("某人在召唤什么东西, 然后%s %s了.", arg,
                             vtense(arg, "出现"));
                else
                    pline("%s %s了.", upstart(arg), vtense(arg, "出现"));
            }

            /* seen caster, possibly producing unseen--or just one--critters;
               hero is told what the caster is doing and doesn't necessarily
               observe complete accuracy of that caster's results (in other
               words, no need to fuss with visibility or singularization;
               player is told what's happening even if hero is unconscious) */
        } else if (!success)
            fmt = "%s 对一堆树枝施法, 但无事发生.";
        else if (let == S_SNAKE)
            fmt = "%s 把一堆树枝变成了蛇!";
        else if (Invisible && !perceives(mtmp->data)
                 && (mtmp->mux != u.ux || mtmp->muy != u.uy))
            fmt = "%s 召唤昆虫到你附近一点的周围!";
        else if (Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy))
            fmt = "%s 召唤昆虫到你的位移幻影的周围!";
        else
            fmt = "%s 召唤昆虫!";
        if (fmt)
            pline(fmt, Monnam(mtmp));

        dmg = 0;
        break;
    }
    case CLC_BLIND_YOU:
        /* note: resists_blnd() doesn't apply here */
        if (!Blinded) {
            int num_eyes = eyecount(youmonst.data);
            pline("鳞片盖住了你的%s!", (num_eyes == 1)
                                               ? body_part(EYE)
                                               : makeplural(body_part(EYE)));
            make_blinded(Half_spell_damage ? 100L : 200L, FALSE);
            if (!Blind)
                Your1(vision_clears);
            dmg = 0;
        } else
            impossible("no reason for monster to cast blindness spell?");
        break;
    case CLC_PARALYZE:
        if (Antimagic || Free_action) {
            shieldeff(u.ux, u.uy);
            if (multi >= 0)
                You("短暂地僵硬了.");
            nomul(-1);
            multi_reason = "被怪物麻痹";
        } else {
            if (multi >= 0)
                You("就地被冰冻了!");
            dmg = 4 + (int) mtmp->m_lev;
            if (Half_spell_damage)
                dmg = (dmg + 1) / 2;
            nomul(-dmg);
            multi_reason = "被怪物麻痹";
        }
        nomovemsg = 0;
        dmg = 0;
        break;
    case CLC_CONFUSE_YOU:
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            You_feel("暂时地混乱.");
        } else {
            boolean oldprop = !!Confusion;

            dmg = (int) mtmp->m_lev;
            if (Half_spell_damage)
                dmg = (dmg + 1) / 2;
            make_confused(HConfusion + dmg, TRUE);
            if (Hallucination)
                You_feel("%s!", oldprop ? "更幻觉" : "幻觉");
            else
                You_feel("%s混乱!", oldprop ? "更加" : "");
        }
        dmg = 0;
        break;
    case CLC_CURE_SELF:
        dmg = m_cure_self(mtmp, dmg);
        break;
    case CLC_OPEN_WOUNDS:
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            dmg = (dmg + 1) / 2;
        }
        if (dmg <= 5)
            Your("皮肤一阵严重的发痒.");
        else if (dmg <= 10)
            pline("你的身体出现伤口!");
        else if (dmg <= 20)
            pline("你的身体出现严重的伤口!");
        else
            Your("身体布满了疼痛的伤口!");
        break;
    default:
        impossible("mcastu: invalid clerical spell (%d)", spellnum);
        dmg = 0;
        break;
    }

    if (dmg)
        mdamageu(mtmp, dmg);
}

STATIC_DCL
boolean
is_undirected_spell(adtyp, spellnum)
unsigned int adtyp;
int spellnum;
{
    if (adtyp == AD_SPEL) {
        switch (spellnum) {
        case MGC_CLONE_WIZ:
        case MGC_SUMMON_MONS:
        case MGC_AGGRAVATION:
        case MGC_DISAPPEAR:
        case MGC_HASTE_SELF:
        case MGC_CURE_SELF:
            return TRUE;
        default:
            break;
        }
    } else if (adtyp == AD_CLRC) {
        switch (spellnum) {
        case CLC_INSECTS:
        case CLC_CURE_SELF:
            return TRUE;
        default:
            break;
        }
    }
    return FALSE;
}

/* Some spells are useless under some circumstances. */
STATIC_DCL
boolean
spell_would_be_useless(mtmp, adtyp, spellnum)
struct monst *mtmp;
unsigned int adtyp;
int spellnum;
{
    /* Some spells don't require the player to really be there and can be cast
     * by the monster when you're invisible, yet still shouldn't be cast when
     * the monster doesn't even think you're there.
     * This check isn't quite right because it always uses your real position.
     * We really want something like "if the monster could see mux, muy".
     */
    boolean mcouldseeu = couldsee(mtmp->mx, mtmp->my);

    if (adtyp == AD_SPEL) {
        /* aggravate monsters, etc. won't be cast by peaceful monsters */
        if (mtmp->mpeaceful
            && (spellnum == MGC_AGGRAVATION || spellnum == MGC_SUMMON_MONS
                || spellnum == MGC_CLONE_WIZ))
            return TRUE;
        /* haste self when already fast */
        if (mtmp->permspeed == MFAST && spellnum == MGC_HASTE_SELF)
            return TRUE;
        /* invisibility when already invisible */
        if ((mtmp->minvis || mtmp->invis_blkd) && spellnum == MGC_DISAPPEAR)
            return TRUE;
        /* peaceful monster won't cast invisibility if you can't see
           invisible,
           same as when monsters drink potions of invisibility.  This doesn't
           really make a lot of sense, but lets the player avoid hitting
           peaceful monsters by mistake */
        if (mtmp->mpeaceful && !See_invisible && spellnum == MGC_DISAPPEAR)
            return TRUE;
        /* healing when already healed */
        if (mtmp->mhp == mtmp->mhpmax && spellnum == MGC_CURE_SELF)
            return TRUE;
        /* don't summon monsters if it doesn't think you're around */
        if (!mcouldseeu && (spellnum == MGC_SUMMON_MONS
                            || (!mtmp->iswiz && spellnum == MGC_CLONE_WIZ)))
            return TRUE;
        if ((!mtmp->iswiz || context.no_of_wizards > 1)
            && spellnum == MGC_CLONE_WIZ)
            return TRUE;
        /* aggravation (global wakeup) when everyone is already active */
        if (spellnum == MGC_AGGRAVATION) {
            /* if nothing needs to be awakened then this spell is useless
               but caster might not realize that [chance to pick it then
               must be very small otherwise caller's many retry attempts
               will eventually end up picking it too often] */
            if (!has_aggravatables(mtmp))
                return rn2(100) ? TRUE : FALSE;
        }
    } else if (adtyp == AD_CLRC) {
        /* summon insects/sticks to snakes won't be cast by peaceful monsters
         */
        if (mtmp->mpeaceful && spellnum == CLC_INSECTS)
            return TRUE;
        /* healing when already healed */
        if (mtmp->mhp == mtmp->mhpmax && spellnum == CLC_CURE_SELF)
            return TRUE;
        /* don't summon insects if it doesn't think you're around */
        if (!mcouldseeu && spellnum == CLC_INSECTS)
            return TRUE;
        /* blindness spell on blinded player */
        if (Blinded && spellnum == CLC_BLIND_YOU)
            return TRUE;
    }
    return FALSE;
}

/* convert 1..10 to 0..9; add 10 for second group (spell casting) */
#define ad_to_typ(k) (10 + (int) k - 1)

/* monster uses spell (ranged) */
int
buzzmu(mtmp, mattk)
register struct monst *mtmp;
register struct attack *mattk;
{
    /* don't print constant stream of curse messages for 'normal'
       spellcasting monsters at range */
    if (mattk->adtyp > AD_SPC2)
        return (0);

    if (mtmp->mcan) {
        cursetxt(mtmp, FALSE);
        return (0);
    }
    if (lined_up(mtmp) && rn2(3)) {
        nomul(0);
        if (mattk->adtyp && (mattk->adtyp < 11)) { /* no cf unsigned >0 */
            if (canseemon(mtmp))
                pline("%s 用%s攻击你!", Monnam(mtmp),
                      flash_types[ad_to_typ(mattk->adtyp)]);
            buzz(-ad_to_typ(mattk->adtyp), (int) mattk->damn, mtmp->mx,
                 mtmp->my, sgn(tbx), sgn(tby));
        } else
            impossible("Monster spell %d cast", mattk->adtyp - 1);
    }
    return (1);
}

/*mcastu.c*/
