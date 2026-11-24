/*
 * AEM30940 harvester status pins. The chip has no registers; it reports
 * on three logic outputs:
 *
 *   STATUS[0]  P0.02  LDOs ready (normal mode)
 *   STATUS[1]  P0.03  battery low: shutdown in ~600 ms
 *   STATUS[2]  P1.15  MPP evaluation (short pulse every ~0.33 s)
 *
 * Shell: "pmic" prints all three.
 */

#ifndef PMIC_H_
#define PMIC_H_

/** Configure the status pins as inputs. */
int pmic_init(void);

#endif /* PMIC_H_ */
