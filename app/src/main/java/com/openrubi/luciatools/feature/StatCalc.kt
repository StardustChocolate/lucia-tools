package com.openrubi.luciatools.feature

import com.openrubi.luciatools.feature.StatScorer.DimParam
import com.openrubi.luciatools.feature.StatScorer.Equip

/**
 * 统一评分实现。
 * 每维：凑递减套(排列表不变) → 每套样板值=(提升总和+加数)
 *       → 总评分 = 该列表的 Lp 范数(P 平衡峰值与数量, 默认 2)
 *       → 雷达值 = 范数 / 满分阈值 × 10  (映射到 0~10)
 */
object StatCalc {

    data class Result(
        val radar: Map<String, Double>,          // 八维雷达值(0~10, 保留两位)
        val radarFull: Map<String, Double>,       // 各维雷达满分参考(统一=10)
        val firstSetSample: Map<String, Double>,  // 各维第一套样板值
        val maxAttack: Int,                       // 顶配攻击力(约束最优)
        val topAttackNote: String,                // 顶配攻击备注
        val maxHp: Int,                           // 顶配生命力
        val topHpNote: String,                    // 顶配生命备注
        val maxDefence: Int,                      // 顶配防御力
        val topDefNote: String,                   // 顶配防御备注
        val maxSpeed: Int,                        // 一速
        val topSpeedNote: String,                 // 一速备注
        val maxResist: Int,                       // 顶配抗性
        val topResistNote: String,                // 顶配抗性备注
        val maxedCount: Int,
        val setSums: Map<String, List<Double>>,   // 各维递减套"提升总和"
        val setParts: Map<String, List<Map<String, Double>>>, // 各维每套的部位分量
        val plusCount: Int
    )

    /** 单件装备某维"提升值" = 固定 + 百分比×百分比基础。subOnly 时只看副属性。 */
    private fun contribution(e: Equip, d: DimParam, subOnly: Boolean): Double {
        val (fixedName, rateName) = StatScorer.PROP_OF[d.key]!!
        val props = if (subOnly) e.subProps else e.allProps
        var fixed = 0.0; var rate = 0.0
        for ((t, v) in props) {
            if (fixedName.isNotEmpty() && t == fixedName) fixed += v
            if (rateName.isNotEmpty() && t == rateName) rate += v
        }
        return fixed + rate * d.pctBase
    }

    /** 项链：attack/defence 维只算副属性(hp 算主+副)——沿用原规则 */
    private fun subOnly(e: Equip, d: DimParam): Boolean =
        e.part == "Necklace" && (d.key == "attack" || d.key == "defence")

    /** 该装备是否因"项链主属性=该维"被整件剔除 */
    private fun excluded(e: Equip, d: DimParam): Boolean {
        if (!d.excludeNecklaceMain || e.part != "Necklace") return false
        val rateName = StatScorer.PROP_OF[d.key]!!.second
        val fixedName = StatScorer.PROP_OF[d.key]!!.first
        return e.mainProps.any { it.first == rateName || (fixedName.isNotEmpty() && it.first == fixedName) }
    }

    /** 凑递减套：返回 (每套提升总和降序, 每套各部位分量降序) */
    private fun stackedSumsDetail(equips: List<Equip>, d: DimParam): Pair<List<Double>, List<Map<String, Double>>> {
        val byPart = HashMap<String, MutableList<Double>>()
        for (p in StatScorer.PARTS) byPart[p] = ArrayList()
        for (e in equips) {
            if (e.part !in byPart) continue
            if (excluded(e, d)) continue
            val c = contribution(e, d, subOnly(e, d))
            if (c > 0) byPart[e.part]!!.add(c)
        }
        byPart.values.forEach { it.sortDescending() }
        val maxSets = byPart.values.maxOfOrNull { it.size } ?: 0
        val sums = ArrayList<Double>()
        val parts = ArrayList<Map<String, Double>>()
        for (k in 0 until maxSets) {
            var s = 0.0
            val comp = LinkedHashMap<String, Double>()
            for (p in StatScorer.PARTS) {
                val v = byPart[p]!!.getOrNull(k)
                if (v != null) { s += v; comp[p] = Math.round(v * 100.0) / 100.0 }
            }
            if (s > 0) { sums.add(s); parts.add(comp) }
        }
        return sums to parts
    }

    /** Lp 范数评分：对每套样板值(提升总和+加数)取 P 次方求和再开 P 次方根。
     *  P 大偏峰值(单件极值主导)，P 小偏数量(累积主导)。 */
    private fun normScore(sums: List<Double>, d: DimParam): Double {
        if (sums.isEmpty()) return 0.0
        val p = StatScorer.NORM_P
        var acc = 0.0
        for (s in sums) {
            val sample = s + d.add
            if (sample > 0) acc += Math.pow(sample, p)
        }
        return Math.pow(acc, 1.0 / p)
    }

    fun compute(all: List<Equip>): Result {
        // 雷达图/极值样本：75级以上(含75) 且 强化+15
        val r15 = all.filter { it.equipLv >= StatScorer.MIN_EQUIP_LV && it.strengthen >= StatScorer.MIN_STRENGTHEN }

        val radar = HashMap<String, Double>()
        val radarFull = HashMap<String, Double>()
        val setSums = HashMap<String, List<Double>>()
        val setParts = HashMap<String, List<Map<String, Double>>>()
        val firstSample = HashMap<String, Double>()

        for (d in StatScorer.DIMS) {
            val (sums, parts) = stackedSumsDetail(r15, d)
            setSums[d.key] = sums
            setParts[d.key] = parts
            // 范数评分 → 映射到 0~10：雷达值 = 范数 / 满分阈值 × 10
            val norm = normScore(sums, d)
            val mapped = if (d.fullThreshold > 0) norm / d.fullThreshold * StatScorer.RADAR_SCALE else 0.0
            radar[d.key] = Math.round(mapped * 100.0) / 100.0
            radarFull[d.key] = StatScorer.RADAR_SCALE   // 满分统一为 10
            // 第一套样板值(提升总和+加数)；无套则 0
            firstSample[d.key] = if (sums.isNotEmpty()) sums[0] + d.add else d.add
        }

        // 75级以上(含)满强化(+15)装备数量
        val maxedCount = all.count {
            it.equipLv >= StatScorer.MIN_EQUIP_LV && it.strengthen >= StatScorer.MIN_STRENGTHEN
        }

        // 顶配攻击力 / 生命力 / 防御力 / 一速 / 抗性(回溯搜索)
        val top = TopAttack.solve(r15)
        val topHp = TopHp.solve(r15)
        val topDef = TopDef.solve(r15)
        val topSpd = TopSpeed.solve(r15)
        val topRes = TopResist.solve(r15)

        return Result(
            radar = radar,
            radarFull = radarFull,
            firstSetSample = firstSample,
            maxAttack = top.attackValue,
            topAttackNote = top.note,
            maxHp = topHp.hpValue,
            topHpNote = topHp.note,
            maxDefence = topDef.value,
            topDefNote = topDef.note,
            maxSpeed = topSpd.value,
            topSpeedNote = topSpd.note,
            maxResist = topRes.value,
            topResistNote = topRes.note,
            maxedCount = maxedCount,
            setSums = setSums,
            setParts = setParts,
            plusCount = r15.size
        )
    }
}
