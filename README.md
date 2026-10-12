# Determining Satisfiability and Point Flat-Foldability of Origami Crease Patterns with SAT Solvers

**Course:** DCC831 Theory and Practice of SMT Solving 2026.2  
**Author:** Túlio Faustino Simões de Sousa

## Overview

The proposed project is to, given a crease pattern, determine, if satisfiable, an assignment of how the folds have to be arranged.

In origami, there are two types of folds, mountain folds (M) and valley folds (V). The set of points (P) and the set of edges (e). The edge becomes a fold once we assign a type of fold to it, be it mountain or valley.

A crease pattern is a plane, usually a square paper that has points and lines that go from point to point, and, given the assignment as said before, can be folded, ideally to a flat state.

The precise definition of everything I say briefly in here is in the literature and will be included in the final presentation and documentation.

## The Math Behind It

The interesting part of this is that the foldability of a point is decidable. That is, there is a set of rules that the folds that go through a point have to follow. Thus, there are assignments for the edges (which we, initially, do not know what type of fold they are) to make the crease be foldable.

The three verifying basis for deciding the foldability of a point are the following:

1. The sum of the angles between the folds must be $\pi$.
2. In a given point P, for every edge that goes through it, being V the set of valley fold edges and M the set of mountain fold edges, we have that:
   $$||V| - |M|| = 2$$
3. For every trio of edges, being $\alpha_{i}$ the angle for the edge $e_{i}$ to $\mathcal{c}_{i+1}$, we have:
   $$((e_{i-1} = e_{i+1}) \wedge ((\alpha_{i} < \alpha_{i-1}) \wedge (\alpha_{i} < \alpha_{i+1}))) \rightarrow (e_{i} = \neg e_{i-1})$$

That is, if the angle between $e_{i}$ and the next edge is smaller than the angle from the last edge to $e_{i}$ and smaller than the angle from the next edge to its next edge, the fold must be the contrary to both of those edges.

These sets of rules are known as Maekawa-Justin and Kawasaki-Justin theorems.

## Project Scope

The more conservative proposing of this project is to create a framework, for any given crease pattern, to produce an assignment that satisfies the rules explained above of mountain and valley folds. To do this, I intend to use already established SAT solvers such as CVC5. I am yet to familiarize myself with actually using one.

## Motivation & Challenges

Why am I doing this? Well, first of all, I am an origami artist! Most origami software out there is built to design models from scratch. They already follow the rules by default because making a pattern that cannot be folded does not make much sense. But especially nowadays, in the age of AI slop, I think it is super important to have a tool that can verify if a generated pattern is actually foldable. Trust me, AI is terrible at designing origami and it is flooding places like Pinterest with fake, impossible patterns.

The biggest challenge here is finding a proof. Even if we can assign mountain and valley folds (what I call an MV assignment) and prove that a single point can fold flat, proving that the *entire* model folds flat is an NP-hard problem. So, I probably won't be able to tackle the global foldability part just yet.

Having said that, for the more ambitious part of this project, I want to see if a pattern can be achieved through purely geometric folds, starting just from the four corners of the paper. I will be diving into Martin's (1998) work and other literature to figure out how to reverse engineer the pattern and find its proof. 

## References

I will be studying these papers to better understand the operations and how to reverse the pattern to find its proof. More will be added later!

* Alperin. "A mathematical theory of origami construction and numbers"
* Martin. "Geometric Constructions"
* Alperin and Lang. "One-, two-, and multi-fold origami axioms"
* Ida. "Computational Origami Construction as Constraint Solving and Rewriting"
* Huzita. "Axiomatic Development of Origami Geometry"